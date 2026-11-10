#include "Misc/AutomationTest.h"
#include "Abilities/HCShapeshiftRules.h"
#include "Audio/HCProceduralAudio.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCShapeshiftTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCShapeshiftRulesTest, "HowCouldItBeYou.Shapeshift.Rules", HCShapeshiftTests::Flags)
bool FHCShapeshiftRulesTest::RunTest(const FString& Parameters)
{
	using R = EHCShapeshiftReject;
	auto Result = [](TFunctionRef<void(FHCShapeshiftCheck&)> Change)
	{
		FHCShapeshiftCheck Check;
		Check.Now = 100.0;
		Check.ReadyTime = 90.0;
		Change(Check);
		return FHCShapeshiftRules::Validate(Check);
	};

	// The validation list of docs/02 §6.3
	TestTrue(TEXT("Everything fine -> allowed"), Result([](FHCShapeshiftCheck&) {}) == R::None);
	TestTrue(TEXT("Not in play (meeting, ejection, intro)"), Result([](FHCShapeshiftCheck& C) { C.bPhasePlaying = false; }) == R::NotPlaying);
	TestTrue(TEXT("Crew can never shapeshift"), Result([](FHCShapeshiftCheck& C) { C.bIsMonster = false; }) == R::NotMonster);
	TestTrue(TEXT("Dead monster"), Result([](FHCShapeshiftCheck& C) { C.bAlive = false; }) == R::Dead);
	TestTrue(TEXT("In a kill lock"), Result([](FHCShapeshiftCheck& C) { C.bKillLocked = true; }) == R::Locked);
	TestTrue(TEXT("Already transforming"), Result([](FHCShapeshiftCheck& C) { C.State = EHCShapeshiftState::Transforming; }) == R::Busy);
	TestTrue(TEXT("Already disguised"), Result([](FHCShapeshiftCheck& C) { C.State = EHCShapeshiftState::Disguised; }) == R::Busy);
	TestTrue(TEXT("While reverting"), Result([](FHCShapeshiftCheck& C) { C.State = EHCShapeshiftState::Reverting; }) == R::Busy);
	TestTrue(TEXT("Cooldown still running"), Result([](FHCShapeshiftCheck& C) { C.ReadyTime = 100.5; }) == R::OnCooldown);
	TestTrue(TEXT("Cooldown just over"), Result([](FHCShapeshiftCheck& C) { C.ReadyTime = 100.0; }) == R::None);
	TestTrue(TEXT("Target dead, gone or the monster itself"), Result([](FHCShapeshiftCheck& C) { C.bTargetValid = false; }) == R::TargetInvalid);
	TestTrue(TEXT("Target is the shape already worn"), Result([](FHCShapeshiftCheck& C) { C.bSameShape = true; }) == R::SameShape);
	// No cooldown at all (lobby option 0) = any time
	TestTrue(TEXT("Cooldown 0 -> allowed at once"), Result([](FHCShapeshiftCheck& C) { C.Now = 0.0; C.ReadyTime = 0.0; }) == R::None);

	// Reverting early: only while disguised
	FHCShapeshiftCheck Revert;
	Revert.State = EHCShapeshiftState::Disguised;
	TestTrue(TEXT("Disguised -> may revert"), FHCShapeshiftRules::CanRevert(Revert));
	Revert.State = EHCShapeshiftState::Transforming;
	TestFalse(TEXT("Not in the middle of the transformation"), FHCShapeshiftRules::CanRevert(Revert));
	Revert.State = EHCShapeshiftState::Ready;
	TestFalse(TEXT("Nothing to revert"), FHCShapeshiftRules::CanRevert(Revert));
	Revert.State = EHCShapeshiftState::Disguised;
	Revert.bPhasePlaying = false;
	TestFalse(TEXT("Not during a meeting"), FHCShapeshiftRules::CanRevert(Revert));

	// Special cases of docs/02 §6.6
	using G = FHCShapeshiftRules::ETargetGone;
	TestTrue(TEXT("Target dies during the transformation -> cancelled"), FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState::Transforming, false, false) == G::Cancel);
	TestTrue(TEXT("Target leaves during the transformation -> cancelled"), FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState::Transforming, true, false) == G::Cancel);
	TestTrue(TEXT("Target dies while copied -> disguise stays"), FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState::Disguised, false, false) == G::Nothing);
	TestTrue(TEXT("...unless the lobby option says revert"), FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState::Disguised, false, true) == G::Revert);
	TestTrue(TEXT("Target disconnects while copied -> revert"), FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState::Disguised, true, false) == G::Revert);
	TestTrue(TEXT("Already reverting -> nothing"), FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState::Reverting, true, false) == G::Nothing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCShapeshiftSoundTest, "HowCouldItBeYou.Shapeshift.Sound", HCShapeshiftTests::Flags)
bool FHCShapeshiftSoundTest::RunTest(const FString& Parameters)
{
	float Seconds = 0.f;
	const TArray<int16> Samples = HCProceduralAudio::Generate(HCProceduralAudio::ESound::Shapeshift, Seconds);
	TestTrue(TEXT("About as long as the transformation (1.5 s)"), Seconds > 1.f && Seconds < 2.f);
	int32 Peak = 0;
	for (const int16 Sample : Samples)
	{
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
	}
	TestTrue(TEXT("Has samples and is audible"), Samples.Num() > 1000 && Peak > 3000);
	TestTrue(TEXT("Does not clip"), Peak <= 32767);
	return true;
}

#endif
