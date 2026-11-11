#include "Misc/AutomationTest.h"
#include "Abilities/HCKillRules.h"
#include "Audio/HCProceduralAudio.h"
#include "Roles/HCWinConditionEvaluator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCKillTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	/** A kill that is allowed: monster next to a living crew member, 40 s into the round. */
	static FHCKillCheck Valid()
	{
		FHCKillCheck Check;
		Check.Now = 40.0;
		Check.KillReadyTime = 20.0;
		Check.Distance = 150.f;
		return Check;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCKillRulesTest, "HowCouldItBeYou.Kill.Rules", HCKillTests::Flags)
bool FHCKillRulesTest::RunTest(const FString& Parameters)
{
	using namespace HCKillTests;
	TestEqual(TEXT("A valid kill is allowed"), FHCKillRules::Validate(Valid()), EHCKillReject::None);

	FHCKillCheck Check = Valid();
	Check.bPhasePlaying = false;
	TestEqual(TEXT("Not outside phase Playing (e.g. in a meeting)"), FHCKillRules::Validate(Check), EHCKillReject::NotPlaying);

	Check = Valid();
	Check.bAttackerIsMonster = false;
	TestEqual(TEXT("Crew can never kill"), FHCKillRules::Validate(Check), EHCKillReject::NotMonster);

	Check = Valid();
	Check.bAttackerAlive = false;
	TestEqual(TEXT("A dead monster cannot kill"), FHCKillRules::Validate(Check), EHCKillReject::AttackerDead);

	Check = Valid();
	Check.bAttackerLocked = true;
	TestEqual(TEXT("Not while the monster is locked in another kill"), FHCKillRules::Validate(Check), EHCKillReject::AttackerLocked);

	Check = Valid();
	Check.bTargetAlive = false;
	TestEqual(TEXT("Dead or disconnected targets are invalid"), FHCKillRules::Validate(Check), EHCKillReject::TargetInvalid);

	Check = Valid();
	Check.bTargetIsMonster = true;
	TestEqual(TEXT("Only crew can be killed"), FHCKillRules::Validate(Check), EHCKillReject::TargetInvalid);

	Check = Valid();
	Check.bTargetLocked = true;
	TestEqual(TEXT("A target that is already being killed is refused"), FHCKillRules::Validate(Check), EHCKillReject::TargetLocked);

	// Cooldown and first-kill delay are the same check: the kill is allowed from KillReadyTime on
	Check = Valid();
	Check.Now = 19.9;
	TestEqual(TEXT("First kill not before 20 s"), FHCKillRules::Validate(Check), EHCKillReject::OnCooldown);
	Check.Now = 20.0;
	TestEqual(TEXT("First kill at 20 s"), FHCKillRules::Validate(Check), EHCKillReject::None);
	Check.KillReadyTime = 20.0 + 2.5 + 30.0;
	Check.Now = 50.0;
	TestEqual(TEXT("Second kill not during the 30 s cooldown"), FHCKillRules::Validate(Check), EHCKillReject::OnCooldown);

	// Range: 200 cm plus 50 cm ping tolerance on the server
	Check = Valid();
	Check.Distance = 200.f;
	TestEqual(TEXT("200 cm is in range"), FHCKillRules::Validate(Check), EHCKillReject::None);
	Check.Distance = 250.f;
	TestEqual(TEXT("250 cm is still accepted by the server (ping tolerance)"), FHCKillRules::Validate(Check), EHCKillReject::None);
	Check.Distance = 251.f;
	TestEqual(TEXT("More than 250 cm is too far"), FHCKillRules::Validate(Check), EHCKillReject::TooFar);

	Check = Valid();
	Check.bLineOfSight = false;
	TestEqual(TEXT("No kill through walls"), FHCKillRules::Validate(Check), EHCKillReject::NoLineOfSight);

	// The meeting room is kill-free for both sides
	Check = Valid();
	Check.bTargetInMeetingRoom = true;
	TestEqual(TEXT("Target in the command center"), FHCKillRules::Validate(Check), EHCKillReject::InMeetingRoom);
	Check = Valid();
	Check.bAttackerInMeetingRoom = true;
	TestEqual(TEXT("Monster in the command center"), FHCKillRules::Validate(Check), EHCKillReject::InMeetingRoom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCKillParityTest, "HowCouldItBeYou.Kill.MonsterWinsAfterSecondKillOfFour", HCKillTests::Flags)
bool FHCKillParityTest::RunTest(const FString& Parameters)
{
	// 4 players = 3 crew + 1 monster (docs/06 M4: the monster wins after the 2nd kill)
	FText Reason;
	FHCWinInput In;
	In.AliveMonsters = 1;
	In.TotalTasks = 18;
	In.AliveCrew = 3;
	TestEqual(TEXT("Start: nobody wins"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::None);
	In.AliveCrew = 2;
	TestEqual(TEXT("After the 1st kill the round goes on"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::None);
	In.AliveCrew = 1;
	TestEqual(TEXT("After the 2nd kill the monster wins"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::Monster);

	// Ghost tasks count: with all tasks done the crew wins even if only one of them is still alive... but parity is checked first
	In.CompletedTasks = 18;
	TestEqual(TEXT("Parity is checked before tasks"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::Monster);
	In.AliveCrew = 2;
	TestEqual(TEXT("2 alive + all tasks (some done by ghosts) -> crew"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::Crew);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCProceduralAudioTest, "HowCouldItBeYou.Audio.PlaceholderSounds", HCKillTests::Flags)
bool FHCProceduralAudioTest::RunTest(const FString& Parameters)
{
	using HCProceduralAudio::ESound;
	for (const ESound Sound : { ESound::Stinger, ESound::Thump, ESound::Scream, ESound::Door })
	{
		float Seconds = 0.f;
		const TArray<int16> Samples = HCProceduralAudio::Generate(Sound, Seconds);
		TestTrue(TEXT("Sound has a sensible length"), Seconds > 0.5f && Seconds < 3.f);
		TestEqual(TEXT("Sample count matches the length at 22.05 kHz"), Samples.Num(), FMath::RoundToInt(Seconds * 22050.f));
		int32 Peak = 0;
		for (const int16 Sample : Samples)
		{
			Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
		}
		TestTrue(TEXT("Sound is not silent and does not clip"), Peak > 3000 && Peak <= 30000);
		// No click at the end: the last samples are (almost) silent
		TestTrue(TEXT("Sound fades out"), FMath::Abs(static_cast<int32>(Samples.Last())) < 2500);
	}
	// The flashlight switch is short and crisp, but must not crack the speakers either
	float ClickSeconds = 0.f;
	const TArray<int16> Click = HCProceduralAudio::Generate(ESound::Click, ClickSeconds);
	TestTrue(TEXT("Click is short"), ClickSeconds > 0.05f && ClickSeconds < 0.3f);
	int32 ClickPeak = 0;
	for (const int16 Sample : Click)
	{
		ClickPeak = FMath::Max(ClickPeak, FMath::Abs(static_cast<int32>(Sample)));
	}
	TestTrue(TEXT("Click is audible and does not clip"), ClickPeak > 3000 && ClickPeak < 30000);
	TestTrue(TEXT("Click dies away"), Click.Num() > 0 && FMath::Abs(static_cast<int32>(Click.Last())) < 300);
	return true;
}

#endif
