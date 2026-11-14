#include "Core/HCGraphics.h"

#include "Core/HCGameInstance.h"
#include "Core/HCLog.h"
#include "Core/HCMenuGameMode.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Scalability.h"
#include "UI/HCFpsOverlay.h"
#include "UnrealClient.h"

const TCHAR* const HCGraphics::QualityChosenKey = TEXT("QualityChosen");
const TCHAR* const HCGraphics::QualityDetectedKey = TEXT("QualityDetected");
const TCHAR* const HCGraphics::ResolutionChosenKey = TEXT("ResolutionChosen");
const TCHAR* const HCGraphics::FpsCounterKey = TEXT("FpsCounter");

namespace HCGraphicsTuning
{
	/** A round costs the graphics card a little more than the main menu (more lamps, flashlights, haze in big rooms). */
	constexpr float RoundPerMenu = 1.25f;
	/** Time for one picture of a round that the chosen level stays under: about 60 pictures per second. */
	constexpr float BudgetMs = 16.f;
	/** Cost of each level (low, medium, high) relative to very high: GPU time in the station at 1920x1080, 2026-11-13. */
	constexpr float LevelCost[4] = { 0.41f, 0.52f, 0.80f, 1.f };
	/** The main menu is on screen this long before measuring (shaders, textures and the first pictures settle). */
	constexpr float SettleSeconds = 3.f;
	constexpr int32 SamplesNeeded = 180;
	/** The faster quarter of the pictures counts: other programs using the graphics card now and then, or a stall
	 *  while a shader loads, only make single pictures slower. */
	constexpr float CountedQuantile = 0.25f;
	/** Without any time from the graphics card after this long in the menu, the measurement is given up. */
	constexpr float GiveUpSeconds = 20.f;
	/** r.AntiAliasingMethod values */
	constexpr int32 TemporalAA = 2;
	constexpr int32 TemporalSuperResolution = 4;
	/** The FPS counter lies on top of every menu (the settings menu is at 500). */
	constexpr int32 FpsOverlayZOrder = 10000;
	/** How often the setting of the FPS counter is looked at. */
	constexpr float FpsSettingSeconds = 0.2f;
}

int32 HCGraphics::OverallLevel(const Scalability::FQualityLevels& Levels)
{
	const int32 Level = Levels.ViewDistanceQuality;
	const bool bSame = Level == Levels.AntiAliasingQuality && Level == Levels.ShadowQuality && Level == Levels.GlobalIlluminationQuality
		&& Level == Levels.ReflectionQuality && Level == Levels.PostProcessQuality && Level == Levels.TextureQuality
		&& Level == Levels.EffectsQuality && Level == Levels.FoliageQuality && Level == Levels.ShadingQuality && Level == Levels.LandscapeQuality;
	return bSame ? Level : INDEX_NONE;
}

void HCGraphics::SetOverallLevel(Scalability::FQualityLevels& Levels, int32 Level)
{
	const float Resolution = Levels.ResolutionQuality;
	Levels.SetFromSingleQualityLevel(FMath::Clamp(Level, 0, 3));
	Levels.ResolutionQuality = Resolution;
}

int32 HCGraphics::BaseLevel(const Scalability::FQualityLevels& Levels)
{
	// View distance has no row of its own in the settings: it still has the level the player last chose for everything
	return FMath::Clamp(Levels.ViewDistanceQuality, 0, 3);
}

float HCGraphics::StepResolution(float CurrentPercent, int32 Delta)
{
	const float Step = FMath::RoundToFloat(FMath::Clamp(CurrentPercent, MinResolutionPercent, 100.f) / ResolutionStepPercent);
	return FMath::Clamp((Step + FMath::Clamp(Delta, -1, 1)) * ResolutionStepPercent, MinResolutionPercent, 100.f);
}

FIntPoint HCGraphics::RenderSize(FIntPoint Window, float Percent)
{
	const float Fraction = FMath::Clamp(Percent, 1.f, 100.f) / 100.f;
	return FIntPoint(FMath::Max(0, FMath::RoundToInt32(Window.X * Fraction)), FMath::Max(0, FMath::RoundToInt32(Window.Y * Fraction)));
}

float HCGraphics::CurrentResolutionPercent()
{
	// 0 = "the project's own" = full resolution
	const float Percent = Scalability::GetQualityLevels().ResolutionQuality;
	return Percent <= 0.f ? 100.f : FMath::Clamp(Percent, 1.f, 100.f);
}

int32 HCGraphics::AntiAliasingMethodFor(int32 AntiAliasingQuality)
{
	// TSR at full resolution costs about 2 ms per picture on an RTX 4070 at 1080p, TAA 0.1 ms
	return AntiAliasingQuality <= 1 ? HCGraphicsTuning::TemporalAA : HCGraphicsTuning::TemporalSuperResolution;
}

int32 HCGraphics::ChooseQualityLevel(float MenuGpuMs)
{
	using namespace HCGraphicsTuning;
	if (MenuGpuMs <= 0.f)
	{
		return 3;
	}
	for (int32 Level = 3; Level > 0; --Level)
	{
		if (MenuGpuMs * RoundPerMenu * LevelCost[Level] <= BudgetMs)
		{
			return Level;
		}
	}
	return 0;
}

FIntPoint HCGraphics::WindowedSize(FIntPoint Desktop)
{
	// Multiples of 8, so that the picture divides evenly
	return FIntPoint(FMath::Max(640, Desktop.X * 4 / 5 / 8 * 8), FMath::Max(360, Desktop.Y * 4 / 5 / 8 * 8));
}

void HCGraphics::ApplyAntiAliasingMethod()
{
	IConsoleManager& Console = IConsoleManager::Get();
	IConsoleVariable* Quality = Console.FindConsoleVariable(TEXT("sg.AntiAliasingQuality"));
	IConsoleVariable* Method = Console.FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
	if (!Quality || !Method)
	{
		return;
	}
	// Set on purpose in the console or on the command line: leave it
	const uint32 SetBy = static_cast<uint32>(Method->GetFlags()) & static_cast<uint32>(ECVF_SetByMask);
	if (SetBy == static_cast<uint32>(ECVF_SetByCommandline) || SetBy > static_cast<uint32>(ECVF_SetByCode))
	{
		return;
	}
	const int32 Wanted = AntiAliasingMethodFor(Quality->GetInt());
	if (Method->GetInt() != Wanted)
	{
		Method->Set(Wanted, ECVF_SetByCode);
		UE_LOG(LogHC, Log, TEXT("Graphics: anti-aliasing %s for quality level %d"),
			Wanted == HCGraphicsTuning::TemporalAA ? TEXT("TAA") : TEXT("TSR"), Quality->GetInt());
	}
}

bool UHCGraphicsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Not in the editor (its viewports have their own settings), not without pictures (server, tests with -nullrhi)
	return !GIsEditor && !IsRunningDedicatedServer() && FApp::CanEverRender() && Super::ShouldCreateSubsystem(Outer);
}

void UHCGraphicsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApplyResolutionRules();
	HCGraphics::ApplyAntiAliasingMethod();
	// Called after console variables changed: a new quality level brings its anti-aliasing method along
	AntiAliasingSink = IConsoleManager::Get().RegisterConsoleVariableSink_Handle(FConsoleCommandDelegate::CreateStatic(&HCGraphics::ApplyAntiAliasingMethod));
	if (ShouldDetectQuality())
	{
		DetectionTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UHCGraphicsSubsystem::TickDetection));
	}
	FpsTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UHCGraphicsSubsystem::TickFpsOverlay), HCGraphicsTuning::FpsSettingSeconds);
}

void UHCGraphicsSubsystem::Deinitialize()
{
	IConsoleManager::Get().UnregisterConsoleVariableSink_Handle(AntiAliasingSink);
	if (DetectionTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DetectionTicker);
		DetectionTicker.Reset();
	}
	if (FpsTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FpsTicker);
		FpsTicker.Reset();
	}
	RemoveFpsOverlay();
	Super::Deinitialize();
}

bool UHCGraphicsSubsystem::TickFpsOverlay(float DeltaTime)
{
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
	const int32 Mode = GI ? FMath::Clamp(FMath::RoundToInt32(GI->GetOption(HCGraphics::FpsCounterKey, 0.f)), 0, HCFps::ModeCount - 1) : HCFps::Off;
	if (Mode == HCFps::Off || !Viewport)
	{
		RemoveFpsOverlay();
		return true;
	}
	if (!FpsOverlay.IsValid() || FpsViewport.Get() != Viewport)
	{
		RemoveFpsOverlay();
		FpsOverlay = SNew(SHCFpsOverlay);
		FpsViewport = Viewport;
		// Stays on the window over every map change (lobby, round, back to the menu)
		Viewport->AddViewportWidgetContent(FpsOverlay.ToSharedRef(), HCGraphicsTuning::FpsOverlayZOrder);
	}
	FpsOverlay->SetMode(Mode);
	return true;
}

void UHCGraphicsSubsystem::RemoveFpsOverlay()
{
	if (FpsOverlay.IsValid())
	{
		if (UGameViewportClient* Viewport = FpsViewport.Get())
		{
			Viewport->RemoveViewportWidgetContent(FpsOverlay.ToSharedRef());
		}
		FpsOverlay.Reset();
	}
	FpsViewport.Reset();
}

void UHCGraphicsSubsystem::ApplyResolutionRules()
{
	UGameUserSettings* Video = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Video)
	{
		return;
	}
	bool bSave = false;
	// Resolution of the 3D picture: older versions lowered it with the quality level (50/71/87 %) without asking, so
	// it goes back to 100 % unless the player picked it in the settings; 0 = never set
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	const bool bChosen = GI && GI->GetOption(HCGraphics::ResolutionChosenKey, 0.f) > 0.5f;
	float Normalized = 0.f, Current = 100.f, Min = 0.f, Max = 100.f;
	Video->GetResolutionScaleInformationEx(Normalized, Current, Min, Max);
	const float Wanted = bChosen && Current > 0.f ? FMath::Clamp(Current, HCGraphics::MinResolutionPercent, 100.f) : 100.f;
	if (!FMath::IsNearlyEqual(Current, Wanted) || Video->IsDynamicResolutionEnabled())
	{
		UE_LOG(LogHC, Log, TEXT("Graphics: render resolution %.0f %% -> %.0f %%"), Current, Wanted);
		Video->SetResolutionScaleValueEx(Wanted);
		Video->SetDynamicResolutionEnabled(false);
		Video->ApplyNonResolutionSettings();
		bSave = true;
	}
	else if (Wanted < 100.f)
	{
		UE_LOG(LogHC, Log, TEXT("Graphics: render resolution %.0f %% (chosen in the settings)"), Wanted);
	}
	// The engine applied the saved levels before the game instance exists, and ApplyNonResolutionSettings leaves the
	// levels alone until the engine has finished starting: set the resolution right away
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	if (!FMath::IsNearlyEqual(Levels.ResolutionQuality, Wanted))
	{
		Levels.ResolutionQuality = Wanted;
		Scalability::SetQualityLevels(Levels);
	}
	// Fullscreen always at the size of the screen (a window may have any size). A window size or mode given on the
	// command line (test runs: -windowed -ResX=...) is left alone.
	const TCHAR* CommandLine = FCommandLine::Get();
	const bool bWindowFromCommandLine = FParse::Param(CommandLine, TEXT("windowed")) || FParse::Param(CommandLine, TEXT("fullscreen"))
		|| FParse::Param(CommandLine, TEXT("RenderOffscreen")) || FCString::Strifind(CommandLine, TEXT("-ResX=")) || FCString::Strifind(CommandLine, TEXT("-ResY="));
	const FIntPoint Desktop = Video->GetDesktopResolution();
	if (!bWindowFromCommandLine && Video->GetFullscreenMode() != EWindowMode::Windowed && Desktop.X > 0 && Desktop.Y > 0
		&& Video->GetScreenResolution() != Desktop)
	{
		UE_LOG(LogHC, Log, TEXT("Graphics: fullscreen %dx%d -> screen size %dx%d"),
			Video->GetScreenResolution().X, Video->GetScreenResolution().Y, Desktop.X, Desktop.Y);
		Video->SetScreenResolution(Desktop);
		Video->ApplyResolutionSettings(false);
		bSave = true;
	}
	if (bSave)
	{
		Video->SaveSettings();
	}
}

bool UHCGraphicsSubsystem::ShouldDetectQuality() const
{
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	const UGameUserSettings* Video = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GI || !Video)
	{
		return false;
	}
	const TCHAR* CommandLine = FCommandLine::Get();
	// -HCAutoQuality measures in any case (development)
	if (!FParse::Param(CommandLine, TEXT("HCAutoQuality")))
	{
		if (GI->GetOption(HCGraphics::QualityDetectedKey, 0.f) > 0.5f || GI->GetOption(HCGraphics::QualityChosenKey, 0.f) > 0.5f)
		{
			return false;
		}
		// Test and development runs (every switch of this game starts with -HC) keep their settings
		if (FCString::Strifind(CommandLine, TEXT("-HC")) || FApp::IsUnattended() || FParse::Param(CommandLine, TEXT("RenderOffscreen")))
		{
			return false;
		}
		if (HCGraphics::OverallLevel(Video->ScalabilityQuality) != 3)
		{
			// Lowered (or mixed) with an older version: that was the player's choice
			UE_LOG(LogHC, Log, TEXT("Graphics: quality level %d chosen before, no automatic choice"), HCGraphics::OverallLevel(Video->ScalabilityQuality));
			GI->SetOption(HCGraphics::QualityDetectedKey, 1.f);
			return false;
		}
	}
	UE_LOG(LogHC, Log, TEXT("Graphics: the main menu will measure the graphics card to choose the quality level"));
	return true;
}

bool UHCGraphicsSubsystem::TickDetection(float DeltaTime)
{
	using namespace HCGraphicsTuning;
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (!GI)
	{
		return true;
	}
	if (GI->GetOption(HCGraphics::QualityChosenKey, 0.f) > 0.5f)
	{
		FinishDetection(INDEX_NONE, 0.f); // the player picked a level in the meantime
		return false;
	}
	// Only while the main menu is on screen: the lobby and a round cost differently. Leaving it starts over next time.
	const UWorld* World = GI->GetWorld();
	if (!World || !World->GetAuthGameMode<AHCMenuGameMode>())
	{
		MenuSeconds = 0.f;
		GpuSamples.Reset();
		return true;
	}
	MenuSeconds += DeltaTime;
	if (MenuSeconds < SettleSeconds)
	{
		return true;
	}
	const float GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	if (GpuMs > 0.f)
	{
		GpuSamples.Add(GpuMs);
	}
	else if (GpuSamples.IsEmpty() && MenuSeconds > GiveUpSeconds)
	{
		FinishDetection(INDEX_NONE, 0.f); // this graphics card reports no times
		return false;
	}
	if (GpuSamples.Num() < SamplesNeeded)
	{
		return true;
	}
	GpuSamples.Sort();
	const float Counted = GpuSamples[FMath::FloorToInt32(GpuSamples.Num() * CountedQuantile)];
	UE_LOG(LogHC, Log, TEXT("Graphics: %d pictures measured, fastest %.1f ms, median %.1f ms, slowest %.1f ms"),
		GpuSamples.Num(), GpuSamples[0], GpuSamples[GpuSamples.Num() / 2], GpuSamples.Last());
	FinishDetection(HCGraphics::ChooseQualityLevel(Counted), Counted);
	return false;
}

void UHCGraphicsSubsystem::FinishDetection(int32 Level, float MenuGpuMs)
{
	DetectionTicker.Reset(); // the ticker removes itself (TickDetection returns false)
	GpuSamples.Reset();
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (GI)
	{
		GI->SetOption(HCGraphics::QualityDetectedKey, 1.f);
	}
	UGameUserSettings* Video = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (Level == INDEX_NONE || !Video)
	{
		UE_LOG(LogHC, Log, TEXT("Graphics: no automatic quality level"));
		return;
	}
	const FIntPoint Size = GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY() : Video->GetScreenResolution();
	UE_LOG(LogHC, Log, TEXT("Graphics: main menu takes %.1f ms per picture on the graphics card at %dx%d, very high quality -> quality level %d"),
		MenuGpuMs, Size.X, Size.Y, Level);
	if (Level < 3 && HCGraphics::OverallLevel(Video->ScalabilityQuality) == 3)
	{
		HCGraphics::SetOverallLevel(Video->ScalabilityQuality, Level);
		Video->ApplyNonResolutionSettings();
		Video->SaveSettings();
	}
}
