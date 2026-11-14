#include "Misc/AutomationTest.h"
#include "Voice/HCVoiceBoost.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCVoiceBoostTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
	constexpr float Rate = 48000.f;

	/** A tone of this level, one second, mono. */
	TArray<float> Tone(float Level, float Seconds = 1.f)
	{
		TArray<float> Samples;
		Samples.SetNumUninitialized(FMath::RoundToInt32(Rate * Seconds));
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			Samples[Index] = Level * FMath::Sin(2.f * PI * 220.f * Index / Rate);
		}
		return Samples;
	}

	/** Runs the samples through in blocks of 480 like the engine does, returns the output. */
	TArray<float> Run(HCVoiceBoostDsp::FState& State, const TArray<float>& In, float Gain)
	{
		TArray<float> Out;
		Out.SetNumZeroed(In.Num());
		for (int32 Start = 0; Start < In.Num(); Start += 480)
		{
			HCVoiceBoostDsp::Process(State, In.GetData() + Start, Out.GetData() + Start, FMath::Min(480, In.Num() - Start), 1, Rate, Gain);
		}
		return Out;
	}

	float PeakOf(const TArray<float>& Samples, int32 From = 0)
	{
		float Peak = 0.f;
		for (int32 Index = From; Index < Samples.Num(); ++Index)
		{
			Peak = FMath::Max(Peak, FMath::Abs(Samples[Index]));
		}
		return Peak;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceBoostQuietTest, "HowCouldItBeYou.Voice.Boost.QuietVoiceGetsLouder", HCVoiceBoostTests::Flags)
bool FHCVoiceBoostQuietTest::RunTest(const FString& Parameters)
{
	using namespace HCVoiceBoostTests;
	// A quiet voice (0.1) at 100 % stays as it is, at 300 % it is three times as loud (the limiter stays out of it)
	HCVoiceBoostDsp::FState Normal;
	TestEqual(TEXT("100 %: unchanged"), PeakOf(Run(Normal, Tone(0.1f), 1.f)), 0.1f, 0.001f);
	HCVoiceBoostDsp::FState Louder;
	Louder.Gain = 3.f;
	TestEqual(TEXT("300 %: three times"), PeakOf(Run(Louder, Tone(0.1f), 3.f)), 0.3f, 0.002f);
	HCVoiceBoostDsp::FState Muted;
	Muted.Gain = 0.f;
	TestEqual(TEXT("0 %: silent"), PeakOf(Run(Muted, Tone(0.1f), 0.f)), 0.f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceBoostLimitTest, "HowCouldItBeYou.Voice.Boost.NeverClips", HCVoiceBoostTests::Flags)
bool FHCVoiceBoostLimitTest::RunTest(const FString& Parameters)
{
	using namespace HCVoiceBoostTests;
	// The loudest settings on a loud voice: never above the ceiling, but still loud (not squeezed to nothing)
	for (const float Gain : { 1.f, 2.f, 4.f, HCVoiceVolumes::MaxGain })
	{
		HCVoiceBoostDsp::FState State;
		const TArray<float> Out = Run(State, Tone(0.8f), Gain);
		const float Peak = PeakOf(Out);
		TestTrue(FString::Printf(TEXT("gain %.0f: peak %.3f under the ceiling"), Gain, Peak), Peak <= HCVoiceBoostDsp::Ceiling + 1.0e-4f);
		TestTrue(FString::Printf(TEXT("gain %.0f: peak %.3f still loud"), Gain, Peak), Peak >= 0.75f);
	}
	// A sudden loud word after silence at 800 %: the first sample is already held
	HCVoiceBoostDsp::FState State;
	State.Gain = HCVoiceVolumes::MaxGain;
	TArray<float> Burst = Tone(0.f, 0.2f);
	Burst.Append(Tone(1.f, 0.2f));
	TestTrue(TEXT("burst: under the ceiling"), PeakOf(Run(State, Burst, HCVoiceVolumes::MaxGain)) <= HCVoiceBoostDsp::Ceiling + 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceBoostGlideTest, "HowCouldItBeYou.Voice.Boost.GainGlides", HCVoiceBoostTests::Flags)
bool FHCVoiceBoostGlideTest::RunTest(const FString& Parameters)
{
	using namespace HCVoiceBoostTests;
	// A changed setting is reached within a quarter second, without a jump from one block to the next
	HCVoiceBoostDsp::FState State;
	const TArray<float> Out = Run(State, Tone(0.1f, 0.5f), 2.f);
	TestTrue(TEXT("first block: not yet doubled"), PeakOf(TArray<float>(Out.GetData(), 480)) < 0.15f);
	TestEqual(TEXT("after 0.25 s: doubled"), PeakOf(Out, 12000), 0.2f, 0.003f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceVolumeStepTest, "HowCouldItBeYou.Voice.Boost.VolumeSteps", HCVoiceBoostTests::Flags)
bool FHCVoiceVolumeStepTest::RunTest(const FString& Parameters)
{
	// 10 % steps up to 100 %, 25 % steps from there to 400 %; an old in-between value (110 %) lands on the next step
	TestEqual(TEXT("90 -> 100"), HCVoiceVolumes::Step(0.9f, 1), 1.f);
	TestEqual(TEXT("100 -> 125"), HCVoiceVolumes::Step(1.f, 1), 1.25f);
	TestEqual(TEXT("125 -> 100"), HCVoiceVolumes::Step(1.25f, -1), 1.f);
	TestEqual(TEXT("100 -> 90"), HCVoiceVolumes::Step(1.f, -1), 0.9f);
	TestEqual(TEXT("110 -> 125"), HCVoiceVolumes::Step(1.1f, 1), 1.25f);
	TestEqual(TEXT("110 -> 100"), HCVoiceVolumes::Step(1.1f, -1), 1.f);
	TestEqual(TEXT("0 stays 0"), HCVoiceVolumes::Step(0.f, -1), 0.f);
	TestEqual(TEXT("400 stays 400"), HCVoiceVolumes::Step(4.f, 1), 4.f);
	int32 Clicks = 0;
	for (float Value = 0.f; Value < HCVoiceVolumes::MaxVolume && Clicks < 100; Value = HCVoiceVolumes::Step(Value, 1))
	{
		++Clicks;
	}
	TestEqual(TEXT("0 to 400 % in 22 clicks"), Clicks, 22);
	TestEqual(TEXT("both at 400 %: capped"), HCVoiceVolumes::GainOf(4.f, 4.f), HCVoiceVolumes::MaxGain);
	TestEqual(TEXT("muted player"), HCVoiceVolumes::GainOf(2.f, 0.f), 0.f);
	return true;
}

#endif
