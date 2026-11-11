#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCStationMapWidget.generated.h"

class UHCStationMapWidget;
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
 * Station map (M), prototype as a list (docs/02 §11.1): everyone sees the power state of the sectors. The Wandler
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

	bool IsOwnerMonster() const;
	void Close();
	void Rebuild();

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> List;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MonsterText;
	UPROPERTY(Transient) TArray<TObjectPtr<UHCMapClickProxy>> Proxies;

	uint32 ShownSignature = 0;
};
