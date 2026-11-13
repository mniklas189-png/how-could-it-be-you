#include "Misc/AutomationTest.h"
#include "Core/HCGraphics.h"
#include "Misc/ConfigCacheIni.h"

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
	// The engine renders the low levels at 50 / 71 / 87 % of the resolution; this game never goes below 100 %
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

#endif
