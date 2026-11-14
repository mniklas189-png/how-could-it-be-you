#include "Misc/AutomationTest.h"
#include "Abilities/HCJumpscareDirector.h"
#include "Audio/HCProceduralAudio.h"
#include "Audio/HCSoundVolumes.h"
#include "Core/HCGameSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCJumpscareTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	constexpr HCProceduralAudio::ESound ScareSounds[] = { HCProceduralAudio::ESound::Hit, HCProceduralAudio::ESound::Thud, HCProceduralAudio::ESound::Heartbeat,
		HCProceduralAudio::ESound::PowerDown, HCProceduralAudio::ESound::Breath, HCProceduralAudio::ESound::Ringing, HCProceduralAudio::ESound::Static };

	float Rms(const TArray<int16>& Samples, float From, float To)
	{
		const int32 A = FMath::Clamp(FMath::RoundToInt(From * 22050.f), 0, Samples.Num());
		const int32 B = FMath::Clamp(FMath::RoundToInt(To * 22050.f), A, Samples.Num());
		double Sum = 0.0;
		for (int32 i = A; i < B; ++i)
		{
			Sum += FMath::Square(static_cast<double>(Samples[i]) / 32768.0);
		}
		return B > A ? static_cast<float>(FMath::Sqrt(Sum / (B - A))) : 0.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJumpscareStyleTest, "HowCouldItBeYou.Kill.JumpscareStyles", HCJumpscareTests::Flags)
bool FHCJumpscareStyleTest::RunTest(const FString& Parameters)
{
	// Every kill picks one of the four scares, never the one this PC showed last
	for (int32 Previous = 0; Previous <= HCScareStyle::Count; ++Previous)
	{
		TSet<int32> Seen;
		for (int32 Step = 0; Step < 100; ++Step)
		{
			const int32 Style = AHCJumpscareDirector::PickStyle(Previous, Step / 100.f);
			TestTrue(TEXT("A scare from 1 to 4"), Style >= 1 && Style <= HCScareStyle::Count);
			TestNotEqual(TEXT("Never the same scare twice in a row"), Style, Previous);
			Seen.Add(Style);
		}
		TestEqual(TEXT("Every other scare can come"), Seen.Num(), Previous == 0 ? HCScareStyle::Count : HCScareStyle::Count - 1);
	}
	TestTrue(TEXT("Even a random number of exactly 1 gives a scare"), AHCJumpscareDirector::PickStyle(4, 1.f) >= 1 && AHCJumpscareDirector::PickStyle(4, 1.f) <= 3);

	// The server must not turn the victim into a ghost while its scare still runs (ping included)
	const UHCGameSettings* Settings = GetDefault<UHCGameSettings>();
	for (int32 Style = 1; Style <= HCScareStyle::Count; ++Style)
	{
		const float Duration = AHCJumpscareDirector::DurationOf(Style);
		TestTrue(TEXT("A scare lasts 2 to 3 seconds"), Duration >= 2.f && Duration <= 3.f);
		TestTrue(TEXT("The server failsafe leaves room for the ping"), Duration + 0.8f <= Settings->JumpscareServerFailsafeSeconds);
		TestTrue(TEXT("The client failsafe comes after the black"), Duration + 1.f <= Settings->JumpscareClientFailsafeSeconds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJumpscareSoundTest, "HowCouldItBeYou.Audio.JumpscareSounds", HCJumpscareTests::Flags)
bool FHCJumpscareSoundTest::RunTest(const FString& Parameters)
{
	using HCProceduralAudio::ESound;
	for (const ESound Sound : HCJumpscareTests::ScareSounds)
	{
		const FString Name = HCProceduralAudio::Name(Sound);
		TestEqual(FString::Printf(TEXT("%s has the Scares volume (settings, Schreck)"), *Name), HCSoundVolumes::GroupOf(Sound), HCSoundVolumes::EGroup::Scares);
		float Seconds = 0.f;
		const TArray<int16> Samples = HCProceduralAudio::Generate(Sound, Seconds);
		TestTrue(FString::Printf(TEXT("%s has a sensible length"), *Name), Seconds >= 0.5f && Seconds < 3.f);
		TestEqual(FString::Printf(TEXT("%s: sample count matches the length"), *Name), Samples.Num(), FMath::RoundToInt(Seconds * 22050.f));
		int32 Peak = 0;
		for (const int16 Sample : Samples)
		{
			Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
		}
		TestTrue(FString::Printf(TEXT("%s is audible and does not clip (peak %d)"), *Name, Peak), Peak > 3000 && Peak <= 30000);
		TestTrue(FString::Printf(TEXT("%s fades out"), *Name), Samples.Num() > 0 && FMath::Abs(static_cast<int32>(Samples.Last())) < 1500);
	}

	// "Signal lost": the stutter sits exactly where the picture freezes, the pop where the tube switches off
	float Seconds = 0.f;
	const TArray<int16> Static = HCProceduralAudio::Generate(ESound::Static, Seconds);
	const float (&Hold)[3] = HCScareTimeline::SignalHolds[0];
	TestTrue(TEXT("Silence before the first strike"), HCJumpscareTests::Rms(Static, 0.f, 0.3f) < 0.01f);
	TestTrue(TEXT("Stutter while the picture is frozen"), HCJumpscareTests::Rms(Static, Hold[0], Hold[1]) > 0.08f);
	TestTrue(TEXT("Quiet again right after the freeze"), HCJumpscareTests::Rms(Static, Hold[1] + 0.01f, HCScareTimeline::SignalFlashes[0] - 0.01f) < 0.01f);
	TestTrue(TEXT("Pop when the tube switches off"), HCJumpscareTests::Rms(Static, HCScareTimeline::SignalCollapseEnd, HCScareTimeline::SignalCollapseEnd + 0.15f) > 0.05f);
	return true;
}

#endif
