#pragma once

#include "CoreMinimal.h"
#include "UI/HCMonitorWidgets.h"
#include "HCVentWidget.generated.h"

class AHCVent;

/**
 * The Wandler's view from inside a ventilation shaft: the room at floor level, seen through the slot under the
 * lifted grille. The number keys crawl to the other hatches of the shaft, left/right (or dragging the mouse) turn
 * the head, the interact key climbs out. While crawling the screen is dark.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCVentViewWidget : public UHCDeviceViewWidget
{
	GENERATED_BODY()

public:
	/** Crawl to the n-th hatch of the list (0 = the nearest). False if that is not possible right now. */
	bool RequestCrawl(int32 LinkIndex);
	/** The screen is dark: on the way to another hatch, or just climbed in. */
	bool IsCrawling() const;
	int32 GetLinkCount() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void PaintView(const FHCDraw& D, const FHCPaintState& S) const override;
	virtual void OnViewPressed(const FVector2D& Local, const FVector2D& Size) override;

	AHCVent* GetVent() const;

	bool bTurnLeft = false;
	bool bTurnRight = false;
	/** Mouse position of the last drag step, to turn the head by the difference. */
	FVector2D LastDrag = FVector2D::ZeroVector;
	bool bDragging = false;
	/** Where the entries of the hatch list were painted last (for clicks). */
	mutable TArray<FBox2D> LinkBoxes;
	/** Until when (FPlatformTime) the screen stays dark; shared by the views of all hatches, because a crawl ends in a new view. */
	static double DarkUntil;
};
