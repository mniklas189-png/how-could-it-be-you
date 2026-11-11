#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCStationMapWidget.generated.h"

class UHCStationMapWidget;
class USizeBox;
class UTextBlock;
class UVerticalBox;

/** Carries the target of one sabotage button (UButton::OnClicked has no parameters). */
UCLASS()
class HOWCOULDITBEYOU_API UHCMapClickProxy : public UObject
{
	GENERATED_BODY()

public:
	uint8 SabotageType = 0;
	FName TargetId;
	TWeakObjectPtr<UHCStationMapWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Station map (M): a drawn plan of the rooms with your own position - no other players, no tasks - and below
 * it the power state of the sectors (docs/02 §11.1). The Wandler
 * sabotages from this very screen, so from the outside sabotaging looks like reading the map.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCStationMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sends the request and closes the map. False if this player is not the Wandler. Also used by the test bots. */
	bool RequestSabotage(uint8 SabotageType, FName TargetId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOwnerMonster() const;
	void Close();
	void Rebuild();

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> List;
	/** The plan of the station is painted into this box. */
	UPROPERTY(Transient) TObjectPtr<USizeBox> MapArea;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MonsterText;
	UPROPERTY(Transient) TArray<TObjectPtr<UHCMapClickProxy>> Proxies;

	uint32 ShownSignature = 0;
};
