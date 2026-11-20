#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Characters/HCGesture.h"
#include "HCEmoteWheel.generated.h"

struct FHCDraw;
struct FHCPaintState;

/**
 * The emote wheel: while the emote key is held, eight gestures lie in a ring around the middle of the screen; the
 * mouse points at one, letting go of the key (or a click) plays it (AHCCharacter::RequestEmote). Esc or letting go in
 * the middle plays nothing.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCEmoteWheelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Plays the emote under the mouse (or none) and closes the wheel. */
	void Choose();
	/** For the test bots: plays this emote and closes. */
	void ChooseEmote(EHCEmote Emote);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void PaintWheel(const FHCDraw& D, const FHCPaintState& S) const;
	/** The slot under the mouse, -1 in the middle. */
	int32 SlotAt(const FVector2D& Local, const FVector2D& Size) const;
	void Close();

	mutable int32 Hot = -1;
	double OpenedAt = 0.0;
	bool bChosen = false;
};
