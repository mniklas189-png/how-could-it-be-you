#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HCTypes.h"
#include "HCLobbyWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UWidget;

/** Lobby panel: join code, LAN address, voice chat rule (chosen by the host), player list, start (host only), walk
 *  around, leave (docs/02 §16.2). */
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
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModeText;
	/** A click on the join code or the address puts it into the clipboard. */
	UFUNCTION() void HandleCopyCode();
	UFUNCTION() void HandleCopyAddress();
	double CodeCopiedUntil = 0.0;
	double AddressCopiedUntil = 0.0;
	/** Voice chat rule: one button per EHCVoiceChatRule; only the host can click them, everybody sees the choice. */
	UFUNCTION() void HandleVoiceMeetings();
	UFUNCTION() void HandleVoiceNear();
	UFUNCTION() void HandleVoiceAlways();
	void SetVoiceRule(EHCVoiceChatRule Rule);
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VoiceTitle;
	UPROPERTY(Transient) TObjectPtr<UWidget> VoiceRow;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> VoiceButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VoiceInfo;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;

	float RefreshTimer = 0.f;
	FString LastPlayerKey;
};
