#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HCGraphics.generated.h"

/**
 * Picture rules that keep the game sharp and smooth on weak PCs (laptops) without taking anything away:
 * - The game always renders at the full resolution of its window. Fullscreen uses the resolution of the screen; there
 *   is no render scale. The quality levels (Game/Config/DefaultScalability.ini) keep every effect on every level and
 *   only compute it more cheaply.
 * - Anti-aliasing follows the quality level: TSR on high and very high, the much cheaper TAA on low and medium (a
 *   scalability file may not set the method, so it is set here).
 * - On the very first start the game measures in the main menu how long the graphics card needs for a picture and
 *   picks the quality level that keeps a round smooth. A level the player chose is never changed.
 */
namespace HCGraphics
{
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
 * Brings the rules above into effect for the game: full resolution at the start (also for settings saved by older
 * versions), the anti-aliasing method after every change of the quality level, and the automatic choice of the
 * quality level on the first start.
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
	/** Old settings may render below 100 % or use a fullscreen resolution below the screen's: both go to full size. */
	void ApplyFullResolution();
	bool ShouldDetectQuality() const;
	bool TickDetection(float DeltaTime);
	void FinishDetection(int32 Level, float MenuGpuMs);

	FConsoleVariableSinkHandle AntiAliasingSink;
	FTSTicker::FDelegateHandle DetectionTicker;

	/** Time the main menu has been on screen, and the measured picture times of the graphics card (ms). */
	float MenuSeconds = 0.f;
	float WaitedSeconds = 0.f;
	TArray<float> GpuSamples;
};
