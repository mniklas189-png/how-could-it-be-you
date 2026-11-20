#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCWardrobeWidget.generated.h"

class ACameraActor;
class AHCWardrobe;
struct FHCDraw;
struct FHCPaintState;

/**
 * The wardrobe window (World/HCWardrobe): the own figure, turned round and seen through a camera in front of it like
 * in a mirror, and on the right the colour of the jacket (every colour only once in the lobby) and the small things:
 * trousers, hair, boots, backpack. Each choice goes to the server at once and is remembered on this PC.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCWardrobeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(AHCWardrobe* InWardrobe) { Wardrobe = InWardrobe; }

	/** Part -1 = colour (Choice = identity), 0..3 = HCOutfit::EPart. Also used by the test bots. */
	void Pick(int32 Part, int32 Choice);

	/** Back to the own eyes. */
	void Close();

	/** The wardrobe or the own figure is gone, or the round started. */
	bool ShouldClose() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** What is under a point of the panel: Part as in Pick, 4 = the done button, -2 = nothing. */
	struct FHit
	{
		int32 Part = -2;
		int32 Choice = INDEX_NONE;
	};
	FHit HitAt(const FVector2D& Local) const;
	void PaintPanel(const FHCDraw& D, const FHCPaintState& S) const;
	bool IsTaken(int32 InIdentity) const;
	void StartMirror();
	void StopMirror();

	TWeakObjectPtr<AHCWardrobe> Wardrobe;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> MirrorCamera;

	/** Shown choice: what the server says, except right after a click (the answer is on its way). */
	int32 Identity = INDEX_NONE;
	uint8 Outfit = 0;
	double LastPick = -100.0;
	double OpenedAt = 0.0;
};
