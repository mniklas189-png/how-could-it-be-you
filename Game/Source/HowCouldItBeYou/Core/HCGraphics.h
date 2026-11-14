#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HCGraphics.generated.h"

class SHCFpsOverlay;
class UGameViewportClient;
namespace Scalability { struct FQualityLevels; }

/**
 * Picture rules that keep the game sharp and smooth on weak PCs (laptops) without taking anything away:
 * - By itself the game renders at the full resolution of its window. Fullscreen uses the resolution of the screen. The
 *   quality levels (Game/Config/DefaultScalability.ini) keep every effect on every level, only compute it more
 *   cheaply, and never lower the resolution.
 * - The player can lower the resolution of the 3D picture in the settings (50 .. 100 % of the window; texts, menus
 *   and the HUD stay sharp) and set single parts of the quality (shadows, textures ...) apart from the level.
 * - Anti-aliasing follows its quality level: TSR on high and very high, the much cheaper TAA on low and medium (a
 *   scalability file may not set the method, so it is set here).
 * - On the very first start the game measures in the main menu how long the graphics card needs for a picture and
 *   picks the quality level that keeps a round smooth. A level the player chose is never changed.
 * - An FPS counter in the corner (off, pictures per second, with details) shows what a setting brings.
 */
namespace HCGraphics
{
	/** Lowest resolution of the 3D picture the settings offer, in percent of the window (the engine's TAA cannot go lower). */
	constexpr float MinResolutionPercent = 50.f;
	constexpr float ResolutionStepPercent = 10.f;

	/**
	 * The level (0 low .. 3 very high) that all parts of the picture quality share, or -1 when the player set parts
	 * apart. Unlike the engine's own check the resolution does not count: it is a setting of its own here.
	 */
	HOWCOULDITBEYOU_API int32 OverallLevel(const Scalability::FQualityLevels& Levels);
	/** Puts every part on one level and keeps the resolution (the engine would set it back to the level's own). */
	HOWCOULDITBEYOU_API void SetOverallLevel(Scalability::FQualityLevels& Levels, int32 Level);
	/** The level to step on from when the parts differ: the one the parts without a row of their own still have. */
	HOWCOULDITBEYOU_API int32 BaseLevel(const Scalability::FQualityLevels& Levels);

	/** One step down (-1) or up (+1) from a resolution in percent: 50, 60 .. 100. */
	HOWCOULDITBEYOU_API float StepResolution(float CurrentPercent, int32 Delta);
	/** Size of the 3D picture in a window of this size at this percent. */
	HOWCOULDITBEYOU_API FIntPoint RenderSize(FIntPoint Window, float Percent);
	/** The resolution of the 3D picture in effect right now, in percent of the window. */
	HOWCOULDITBEYOU_API float CurrentResolutionPercent();

	/** Option set once the player picked a resolution in the settings menu: it is then kept over starts. */
	HOWCOULDITBEYOU_API extern const TCHAR* const ResolutionChosenKey;
	/** Option: the FPS counter (HCFps::EMode: 0 off, 1 pictures per second, 2 with details). */
	HOWCOULDITBEYOU_API extern const TCHAR* const FpsCounterKey;

	/** r.AntiAliasingMethod for an anti-aliasing quality level (sg.AntiAliasingQuality): 2 = TAA, 4 = TSR. */
	HOWCOULDITBEYOU_API int32 AntiAliasingMethodFor(int32 AntiAliasingQuality);

	/**
	 * The quality level (0 low .. 3 very high) for a graphics card that needs MenuGpuMs for a picture of the main menu
	 * at the very high level: the best level whose expected time for a picture of a round stays within the budget.
	 * 3 when nothing was measured (MenuGpuMs <= 0).
	 */
	HOWCOULDITBEYOU_API int32 ChooseQualityLevel(float MenuGpuMs);

	/** Size of the window when the player switches to windowed mode: four fifths of the screen, same shape. */
	HOWCOULDITBEYOU_API FIntPoint WindowedSize(FIntPoint Desktop);

	/** Option (UHCGameInstance::GetOption) set when the player picks a quality level in the settings menu. */
	HOWCOULDITBEYOU_API extern const TCHAR* const QualityChosenKey;
	/** Option set once the automatic choice on the first start is done (or not needed). */
	HOWCOULDITBEYOU_API extern const TCHAR* const QualityDetectedKey;

	/** Sets r.AntiAliasingMethod to match sg.AntiAliasingQuality (unless the console or command line set it). */
	HOWCOULDITBEYOU_API void ApplyAntiAliasingMethod();
}

/**
 * Brings the rules above into effect for the game: full resolution at the start unless the player chose a lower one
 * (settings saved by older versions lowered it without asking), the anti-aliasing method after every change of the
 * quality level, the automatic choice of the quality level on the first start, and the FPS counter.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCGraphicsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/**
	 * Old settings may render below 100 % (the engine's quality levels did that) or use a fullscreen resolution below
	 * the screen's: both go to full size. A resolution the player picked in the settings stays.
	 */
	void ApplyResolutionRules();
	bool ShouldDetectQuality() const;
	bool TickDetection(float DeltaTime);
	void FinishDetection(int32 Level, float MenuGpuMs);
	/** Puts the FPS counter on the game's window or takes it off, following the setting. */
	bool TickFpsOverlay(float DeltaTime);
	void RemoveFpsOverlay();

	FConsoleVariableSinkHandle AntiAliasingSink;
	FTSTicker::FDelegateHandle DetectionTicker;
	FTSTicker::FDelegateHandle FpsTicker;
	TSharedPtr<SHCFpsOverlay> FpsOverlay;
	TWeakObjectPtr<UGameViewportClient> FpsViewport;

	/** Time the main menu has been on screen, and the measured picture times of the graphics card (ms). */
	float MenuSeconds = 0.f;
	float WaitedSeconds = 0.f;
	TArray<float> GpuSamples;
};
