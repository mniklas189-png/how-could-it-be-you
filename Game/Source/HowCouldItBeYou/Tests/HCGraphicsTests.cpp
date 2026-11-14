#include "Misc/AutomationTest.h"
#include "Core/HCGraphics.h"
#include "Misc/ConfigCacheIni.h"
#include "Scalability.h"
#include "UI/HCFpsOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCGraphicsTest
{
	/** A console variable of a quality level as the game reads it (engine BaseScalability.ini + DefaultScalability.ini). */
	float LevelValue(const FString& Group, int32 Level, const TCHAR* Variable, float Missing = -1000.f)
	{
		FString Value;
		if (!GConfig || !GConfig->GetString(*FString::Printf(TEXT("%s@%d"), *Group, Level), Variable, Value, GScalabilityIni))
		{
			return Missing;
		}
		return FCString::Atof(*Value);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsFullResolutionTest, "HowCouldItBeYou.Graphics.FullResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsFullResolutionTest::RunTest(const FString& Parameters)
{
	// The engine renders the low levels at 50 / 71 / 87 % of the resolution; in this game a quality level never
	// lowers it (only the player does, with the resolution row of the settings)
	TArray<FString> Values;
	GConfig->GetSingleLineArray(TEXT("ScalabilitySettings"), TEXT("PerfIndexValues_ResolutionQuality"), Values, GScalabilityIni);
	TestTrue(TEXT("A render resolution for every level"), Values.Num() >= 4);
	for (int32 Level = 0; Level < Values.Num(); ++Level)
	{
		TestEqual(FString::Printf(TEXT("Level %d renders at full resolution"), Level), FCString::Atof(*Values[Level]), 100.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsLevelsKeepEffectsTest, "HowCouldItBeYou.Graphics.LevelsKeepEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsLevelsKeepEffectsTest::RunTest(const FString& Parameters)
{
	using HCGraphicsTest::LevelValue;
	// Lower levels compute things more cheaply, but every level shows everything the game uses
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		const FString At = FString::Printf(TEXT(" (level %d)"), Level);
		TestTrue(TEXT("Shadows of the flashlights") + At, LevelValue(TEXT("ShadowQuality"), Level, TEXT("r.ShadowQuality")) >= 1.f);
		TestEqual(TEXT("Light cones in the haze (volumetric fog)") + At, LevelValue(TEXT("ShadowQuality"), Level, TEXT("r.VolumetricFog")), 1.f);
		TestTrue(TEXT("Vignette and film grain (unease, jumpscares)") + At, LevelValue(TEXT("PostProcessQuality"), Level, TEXT("r.Tonemapper.Quality")) >= 4.f);
		TestTrue(TEXT("Colour fringe of the jumpscares") + At, LevelValue(TEXT("PostProcessQuality"), Level, TEXT("r.SceneColorFringeQuality")) >= 1.f);
		TestTrue(TEXT("Ambient occlusion") + At, LevelValue(TEXT("PostProcessQuality"), Level, TEXT("r.AmbientOcclusionLevels")) != 0.f
			&& LevelValue(TEXT("PostProcessQuality"), Level, TEXT("r.AmbientOcclusionMaxQuality")) > 0.f);
		TestTrue(TEXT("Bloom of the lamps") + At, LevelValue(TEXT("PostProcessQuality"), Level, TEXT("r.BloomQuality")) >= 1.f);
		TestTrue(TEXT("Screen space reflections") + At, LevelValue(TEXT("ReflectionQuality"), Level, TEXT("r.SSR.Quality")) >= 1.f);
		TestEqual(TEXT("Sharp textures") + At, LevelValue(TEXT("TextureQuality"), Level, TEXT("r.Streaming.MipBias")), 0.f);
		TestEqual(TEXT("Same sky light brightness") + At, LevelValue(TEXT("GlobalIlluminationQuality"), Level, TEXT("r.SkylightIntensityMultiplier"), 1.f), 1.f);
		// r.AntiAliasingMethod may not be set by a scalability file (the engine ignores it with an error): HCGraphics does it
		TestEqual(TEXT("No anti-aliasing method in the scalability file") + At, LevelValue(TEXT("AntiAliasingQuality"), Level, TEXT("r.AntiAliasingMethod")), -1000.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsAntiAliasingTest, "HowCouldItBeYou.Graphics.AntiAliasing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsAntiAliasingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Low: TAA"), HCGraphics::AntiAliasingMethodFor(0), 2);
	TestEqual(TEXT("Medium: TAA"), HCGraphics::AntiAliasingMethodFor(1), 2);
	TestEqual(TEXT("High: TSR"), HCGraphics::AntiAliasingMethodFor(2), 4);
	TestEqual(TEXT("Very high: TSR"), HCGraphics::AntiAliasingMethodFor(3), 4);
	TestEqual(TEXT("Cinematic: TSR"), HCGraphics::AntiAliasingMethodFor(4), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsQualityChoiceTest, "HowCouldItBeYou.Graphics.QualityChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsQualityChoiceTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Nothing measured: no change"), HCGraphics::ChooseQualityLevel(0.f), 3);
	TestEqual(TEXT("Fast graphics card (RTX 4070 at 1080p): very high"), HCGraphics::ChooseQualityLevel(9.f), 3);
	TestEqual(TEXT("A bit slower: high"), HCGraphics::ChooseQualityLevel(15.f), 2);
	TestEqual(TEXT("Laptop graphics card: medium"), HCGraphics::ChooseQualityLevel(22.f), 1);
	TestEqual(TEXT("Integrated graphics: low"), HCGraphics::ChooseQualityLevel(60.f), 0);
	// A slower graphics card never gets a better level
	int32 Previous = 3;
	for (float Ms = 1.f; Ms < 120.f; Ms += 0.5f)
	{
		const int32 Level = HCGraphics::ChooseQualityLevel(Ms);
		TestTrue(FString::Printf(TEXT("%.1f ms: level does not go up"), Ms), Level <= Previous && Level >= 0);
		Previous = Level;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsWindowSizeTest, "HowCouldItBeYou.Graphics.WindowSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsWindowSizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("1080p screen"), HCGraphics::WindowedSize(FIntPoint(1920, 1080)), FIntPoint(1536, 864));
	TestEqual(TEXT("1440p screen"), HCGraphics::WindowedSize(FIntPoint(2560, 1440)), FIntPoint(2048, 1152));
	for (const FIntPoint Screen : { FIntPoint(1366, 768), FIntPoint(2880, 1800), FIntPoint(3840, 2160) })
	{
		const FIntPoint Window = HCGraphics::WindowedSize(Screen);
		TestTrue(TEXT("Smaller than the screen"), Window.X < Screen.X && Window.Y < Screen.Y);
		TestTrue(TEXT("Multiples of 8"), Window.X % 8 == 0 && Window.Y % 8 == 0);
	}
	TestEqual(TEXT("Unknown screen"), HCGraphics::WindowedSize(FIntPoint(0, 0)), FIntPoint(640, 360));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsResolutionStepsTest, "HowCouldItBeYou.Graphics.ResolutionSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsResolutionStepsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("One step down from full"), HCGraphics::StepResolution(100.f, -1), 90.f);
	TestEqual(TEXT("Never above full"), HCGraphics::StepResolution(100.f, 1), 100.f);
	// Steps of 10 down to 50 %, steps of 5 below, down to 25 %
	TestEqual(TEXT("Back up from half"), HCGraphics::StepResolution(50.f, 1), 60.f);
	TestEqual(TEXT("Below half in finer steps"), HCGraphics::StepResolution(50.f, -1), 45.f);
	TestEqual(TEXT("45 % down"), HCGraphics::StepResolution(45.f, -1), 40.f);
	TestEqual(TEXT("45 % up"), HCGraphics::StepResolution(45.f, 1), 50.f);
	TestEqual(TEXT("30 % down: the lowest"), HCGraphics::StepResolution(30.f, -1), 25.f);
	TestEqual(TEXT("Never below the lowest"), HCGraphics::StepResolution(25.f, -1), 25.f);
	TestEqual(TEXT("The lowest is 25 %"), HCGraphics::MinResolutionPercent, 25.f);
	// Values of older versions (the engine's 71 and 87 %) land on the steps
	TestEqual(TEXT("71 % up"), HCGraphics::StepResolution(71.f, 1), 80.f);
	TestEqual(TEXT("87 % down"), HCGraphics::StepResolution(87.f, -1), 80.f);
	TestEqual(TEXT("Far too low a value"), HCGraphics::StepResolution(10.f, 1), 30.f);
	TestEqual(TEXT("Far too high a value"), HCGraphics::StepResolution(400.f, -1), 90.f);
	// From full down to the lowest and back up: every step is its own value, and the way back passes the same ones
	TArray<float> Down;
	float Percent = 100.f;
	while (Percent > HCGraphics::MinResolutionPercent && Down.Num() < 100)
	{
		const float Next = HCGraphics::StepResolution(Percent, -1);
		TestTrue(TEXT("A step down is lower"), Next < Percent);
		Percent = Next;
		Down.Add(Percent);
	}
	TestEqual(TEXT("Steps from 100 % to 25 %"), Down.Num(), 10);
	for (int32 Index = Down.Num() - 1; Index > 0; --Index)
	{
		TestEqual(FString::Printf(TEXT("Up from %.0f %%"), Down[Index]), HCGraphics::StepResolution(Down[Index], 1), Down[Index - 1]);
	}

	// The size the settings show is the size the engine computes (rounded up)
	TestEqual(TEXT("Full size"), HCGraphics::RenderSize(FIntPoint(1920, 1080), 100.f), FIntPoint(1920, 1080));
	TestEqual(TEXT("80 % of 1080p"), HCGraphics::RenderSize(FIntPoint(1920, 1080), 80.f), FIntPoint(1536, 864));
	TestEqual(TEXT("70 % of 1080p"), HCGraphics::RenderSize(FIntPoint(1920, 1080), 70.f), FIntPoint(1344, 756));
	TestEqual(TEXT("50 % of 1440p"), HCGraphics::RenderSize(FIntPoint(2560, 1440), 50.f), FIntPoint(1280, 720));
	TestEqual(TEXT("25 % of 1080p"), HCGraphics::RenderSize(FIntPoint(1920, 1080), 25.f), FIntPoint(480, 270));
	TestEqual(TEXT("35 % of a laptop screen"), HCGraphics::RenderSize(FIntPoint(1366, 768), 35.f), FIntPoint(479, 269));
	TestEqual(TEXT("No window"), HCGraphics::RenderSize(FIntPoint(0, 0), 70.f), FIntPoint(0, 0));

	// The engine's TAA scales the picture up itself only from half the size on
	TestTrue(TEXT("Full resolution: TAA upscales"), HCGraphics::TemporalUpsamplingFor(100.f));
	TestTrue(TEXT("50 %: TAA upscales"), HCGraphics::TemporalUpsamplingFor(50.f));
	TestFalse(TEXT("45 %: plain upscale"), HCGraphics::TemporalUpsamplingFor(45.f));
	TestFalse(TEXT("25 %: plain upscale"), HCGraphics::TemporalUpsamplingFor(25.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsFrameLimitTest, "HowCouldItBeYou.Graphics.FrameLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsFrameLimitTest::RunTest(const FString& Parameters)
{
	// 0 = no limit, the last step upwards
	TestEqual(TEXT("No limit: down to 240"), HCGraphics::StepFrameLimit(0.f, -1), 240.f);
	TestEqual(TEXT("240: up to no limit"), HCGraphics::StepFrameLimit(240.f, 1), 0.f);
	TestEqual(TEXT("No limit stays the top"), HCGraphics::StepFrameLimit(0.f, 1), 0.f);
	TestEqual(TEXT("60 down"), HCGraphics::StepFrameLimit(60.f, -1), 30.f);
	// Below 30 for PCs that do not reach it
	TestEqual(TEXT("30 down"), HCGraphics::StepFrameLimit(30.f, -1), 25.f);
	TestEqual(TEXT("25 down"), HCGraphics::StepFrameLimit(25.f, -1), 20.f);
	TestEqual(TEXT("20 down"), HCGraphics::StepFrameLimit(20.f, -1), 15.f);
	TestEqual(TEXT("15 is the lowest"), HCGraphics::StepFrameLimit(15.f, -1), 15.f);
	TestEqual(TEXT("15 up"), HCGraphics::StepFrameLimit(15.f, 1), 20.f);
	// A limit from somewhere else (console, an edited file) counts as none
	TestEqual(TEXT("Unknown limit: down to 240"), HCGraphics::StepFrameLimit(75.f, -1), 240.f);
	// From no limit down to the lowest: every step is lower, none is skipped on the way back
	TArray<float> Down;
	float Limit = 0.f;
	for (int32 Step = 0; Step < 100; ++Step)
	{
		const float Next = HCGraphics::StepFrameLimit(Limit, -1);
		if (Next == Limit)
		{
			break;
		}
		TestTrue(TEXT("A step down is a lower limit"), Limit == 0.f || Next < Limit);
		Limit = Next;
		Down.Add(Limit);
	}
	TestEqual(TEXT("Limits below \"none\""), Down.Num(), 10);
	TestEqual(TEXT("The lowest limit"), Limit, 15.f);
	for (int32 Index = Down.Num() - 1; Index > 0; --Index)
	{
		TestEqual(FString::Printf(TEXT("Up from %.0f"), Down[Index]), HCGraphics::StepFrameLimit(Down[Index], 1), Down[Index - 1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsQualityPartsTest, "HowCouldItBeYou.Graphics.QualityParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsQualityPartsTest::RunTest(const FString& Parameters)
{
	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(3);
	Levels.ResolutionQuality = 70.f;
	// The engine's own check calls this "mixed" because of the resolution; here the resolution is a setting of its own
	TestEqual(TEXT("Very high at 70 % resolution is still very high"), HCGraphics::OverallLevel(Levels), 3);
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		HCGraphics::SetOverallLevel(Levels, Level);
		TestEqual(FString::Printf(TEXT("Level %d set"), Level), HCGraphics::OverallLevel(Levels), Level);
		TestEqual(FString::Printf(TEXT("Level %d keeps the chosen resolution"), Level), Levels.ResolutionQuality, 70.f);
		TestEqual(FString::Printf(TEXT("Level %d: shadows follow"), Level), Levels.ShadowQuality, Level);
	}
	HCGraphics::SetOverallLevel(Levels, 9);
	TestEqual(TEXT("No level above very high"), HCGraphics::OverallLevel(Levels), 3);

	// One part set apart: no common level any more, and the next step of the level starts from the rest
	HCGraphics::SetOverallLevel(Levels, 1);
	Levels.TextureQuality = 3;
	TestEqual(TEXT("Parts apart"), HCGraphics::OverallLevel(Levels), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("The rest is still medium"), HCGraphics::BaseLevel(Levels), 1);
	HCGraphics::SetOverallLevel(Levels, HCGraphics::BaseLevel(Levels) + 1);
	TestEqual(TEXT("One level for everything again"), HCGraphics::OverallLevel(Levels), 2);
	TestEqual(TEXT("Textures follow again"), Levels.TextureQuality, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCGraphicsFpsCounterTest, "HowCouldItBeYou.Graphics.FpsCounter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCGraphicsFpsCounterTest::RunTest(const FString& Parameters)
{
	FHCFpsMeter Meter;
	TestEqual(TEXT("Nothing to show before the first span"), HCFps::FpsText(Meter), FString());
	TestEqual(TEXT("No details before the first span"), HCFps::DetailText(Meter, FIntPoint(1536, 864)), FString());
	// 17 ms per picture (59 pictures per second), the graphics card needs 9.8 ms for each: a span of half a second is
	// over with the 30th picture
	int32 Ready = 0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Ready += Meter.AddFrame(0.017, 9.8f) ? 1 : 0;
	}
	TestEqual(TEXT("Two new numbers per second"), Ready, 2);
	TestEqual(TEXT("59 pictures per second"), FMath::RoundToInt32(Meter.Fps), 59);
	TestTrue(TEXT("17 ms per picture"), FMath::IsNearlyEqual(Meter.FrameMs, 17.f, 0.01f));
	TestTrue(TEXT("Time of the graphics card"), FMath::IsNearlyEqual(Meter.GpuMs, 9.8f, 0.01f));
	TestEqual(TEXT("First line"), HCFps::FpsText(Meter), FString(TEXT("59 FPS")));
	const FString Detailed = HCFps::DetailText(Meter, FIntPoint(1536, 864));
	TestTrue(TEXT("Details start with the time per picture"), Detailed.StartsWith(TEXT("17.0 ms")));
	TestTrue(TEXT("Details: graphics card"), Detailed.Contains(TEXT("GPU 9.8 ms")));
	TestTrue(TEXT("Details end with the size of the 3D picture"), Detailed.EndsWith(TEXT("1536 x 864")));

	// A graphics card that reports no time: that part is left out
	Meter.Reset();
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Meter.AddFrame(1.0 / 30.0, 0.f);
	}
	TestEqual(TEXT("30 pictures per second"), FMath::RoundToInt32(Meter.Fps), 30);
	TestFalse(TEXT("No graphics card time shown"), HCFps::DetailText(Meter, FIntPoint(1280, 720)).Contains(TEXT("GPU")));
	TestFalse(TEXT("No size without a window"), HCFps::DetailText(Meter, FIntPoint(0, 0)).Contains(TEXT(" x ")));

	// The counter follows the size of the window only within limits
	TestEqual(TEXT("1080p: as large as the menus"), HCFps::OverlayScale(1.f), 1.f);
	TestEqual(TEXT("720p: not as small as the menus"), HCFps::OverlayScale(0.667f), HCFps::SmallestScale);
	TestEqual(TEXT("2160p: stays above the mini map"), HCFps::OverlayScale(2.f), HCFps::LargestScale);

	// A stall (loading a map) is not counted as a slow picture
	Meter.Reset();
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Meter.AddFrame(0.01, 0.f);
	}
	TestFalse(TEXT("A stall gives no number"), Meter.AddFrame(5.0, 0.f));
	Ready = 0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Ready += Meter.AddFrame(0.01, 0.f) ? 1 : 0;
	}
	TestEqual(TEXT("The span started again after the stall"), Ready, 1);
	TestEqual(TEXT("100 pictures per second after the stall"), FMath::RoundToInt32(Meter.Fps), 100);
	return true;
}

#endif
