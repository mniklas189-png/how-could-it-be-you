#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCRoleRevealWidget.generated.h"

/**
 * The private role card at the start of a round (phase RoleReveal, docs/01 §13): CREW or DER WANDLER, the own
 * identity (colour, symbol, name) and the three things to do with their keys. Shown once the own role with the
 * current serial has arrived (docs/02 §5.4). Same timing, layout and sound for both roles (docs/01 §13).
 * Painted by hand (FHCPainter); takes no input.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCRoleRevealWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Local clock from the moment the own role arrived; 0 = not yet. */
	double RevealedAt = 0.0;
};
