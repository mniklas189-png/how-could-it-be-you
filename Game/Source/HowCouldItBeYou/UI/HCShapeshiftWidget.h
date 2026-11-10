#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCShapeshiftWidget.generated.h"

class UHCShapeshiftWidget;
class UVerticalBox;

/** Carries the target of one button (UButton::OnClicked has no parameters). */
UCLASS()
class HOWCOULDITBEYOU_API UHCShapePickProxy : public UObject
{
	GENERATED_BODY()

public:
	int32 TargetPlayerId = -1;
	TWeakObjectPtr<UHCShapeshiftWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Prototype of the shapeshift menu (docs/02 §6.3): the Wandler picks a living player with a click or the number
 * keys; Q or Esc closes it. Only ever opened for the Wandler.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCShapeshiftWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sends the request and closes the menu. False if that player cannot be picked. Also used by the test bots. */
	bool Pick(int32 TargetPlayerId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void Close();
	void Rebuild();

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerList;
	UPROPERTY(Transient) TArray<TObjectPtr<UHCShapePickProxy>> Proxies;

	/** Player ids in the order shown (number keys 1-8). */
	TArray<int32> ShownIds;
	uint32 ShownSignature = 0;
};
