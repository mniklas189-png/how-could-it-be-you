#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "HCMonitorWidgets.generated.h"

class AHCAdminTable;
class AHCMonitorDevice;
class AHCSecurityCamera;
struct FHCDraw;
struct FHCPaintState;

/**
 * Full-screen view of a monitor device (camera desk, admin table). The player stands still while it is open; E or
 * Esc closes it, and the HUD closes it when the round phase changes, the player dies or the power fails.
 */
UCLASS(Abstract)
class HOWCOULDITBEYOU_API UHCDeviceViewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(AHCMonitorDevice* InDevice);
	AHCMonitorDevice* GetDevice() const;
	/** The device went away or lost its power: the HUD closes the view. */
	bool ShouldClose() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	void RequestClose();

	/** The whole screen is one painted control; subclasses draw it and react to clicks. */
	virtual void PaintView(const FHCDraw& D, const FHCPaintState& S) const {}
	virtual void OnViewPressed(const FVector2D& Local, const FVector2D& Size) {}
	/** The common frame: dark screen, title, a line below it, the key hint at the bottom. */
	void PaintFrame(const FHCDraw& D, const FString& Title, const FString& Subtitle, const FString& Hint) const;
	/** Clock of the ship shown on the pictures (counts with the round). */
	FString ClockText() const;

	TWeakObjectPtr<AHCMonitorDevice> Device;
};

/** The camera monitors (Among Us "Security"): all cameras at once in a grid, or one large. */
UCLASS()
class HOWCOULDITBEYOU_API UHCSecurityViewWidget : public UHCDeviceViewWidget
{
	GENERATED_BODY()

public:
	/** Some camera view is open on this machine (it renders the pictures, so the desk does not need to). */
	static bool IsAnyOpen() { return OpenViews > 0; }
	/** -1 = grid, else the camera shown large (test bots; players use keys and clicks). */
	void SetFocusedCamera(int32 Index) { Focused = Cameras.IsValidIndex(Index) ? Index : -1; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void PaintView(const FHCDraw& D, const FHCPaintState& S) const override;
	virtual void OnViewPressed(const FVector2D& Local, const FVector2D& Size) override;

	/** Where the picture of camera Index goes (in the units of the painted control); empty when it is not shown. */
	FBox2D GetFeedRect(int32 Index, const FVector2D& Size) const;
	void PaintFeed(const FHCDraw& D, int32 Index, const FBox2D& Rect, bool bHovered) const;
	void RefreshCameras();

	TArray<TWeakObjectPtr<AHCSecurityCamera>> Cameras;
	TArray<FSlateBrush> Brushes;
	/** -1 = all cameras in a grid, else the camera shown large. */
	int32 Focused = -1;
	static int32 OpenViews;
};

/** The admin table (Among Us "Admin"): the plan of the ship with one dot per person in every room - never who. */
UCLASS()
class HOWCOULDITBEYOU_API UHCAdminViewWidget : public UHCDeviceViewWidget
{
	GENERATED_BODY()

protected:
	virtual void PaintView(const FHCDraw& D, const FHCPaintState& S) const override;

	/** Wall lines of the plan in grid units (doors are left open), worked out once. */
	void BuildWalls() const;
	mutable TArray<TPair<FVector2D, FVector2D>> Walls;
	mutable bool bWallsBuilt = false;
};
