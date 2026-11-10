#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCLobbyWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

/** Lobby panel: join code, LAN address, player list, start (host only), walk around, leave (docs/02 §16.2). */
UCLASS()
class HOWCOULDITBEYOU_API UHCLobbyWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION() void HandleStart();
	UFUNCTION() void HandleWalk();
	UFUNCTION() void HandleLeave();

	void Refresh();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> CodeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AddressText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;

	float RefreshTimer = 0.f;
	FString LastPlayerKey;
};
