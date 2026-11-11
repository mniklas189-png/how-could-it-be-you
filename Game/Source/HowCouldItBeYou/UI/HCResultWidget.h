#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCResultWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;

/** Result screen: winner, reason, all roles with the monster highlighted, countdown, back to lobby (docs/01 §13). */
UCLASS()
class HOWCOULDITBEYOU_API UHCResultWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION() void HandleBackToLobby();

	void Rebuild();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> OutcomeText;
	UPROPERTY(Transient) TObjectPtr<UBorder> TintPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> AccentLine;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WinnerText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ReasonText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> RoleList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountdownText;
	UPROPERTY(Transient) TObjectPtr<UButton> LobbyButton;

	int32 ShownRoles = -1;
	bool bSoundPlayed = false;
	double LastTickSeconds = 0.0;
};
