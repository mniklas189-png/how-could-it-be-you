#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCMainMenuWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UWidget;

/** Main menu: epilepsy warning on first start, name, host lobby, join by code or IP, quit (docs/02 §16.2). */
UCLASS()
class HOWCOULDITBEYOU_API UHCMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION() void HandleAcceptWarning();
	UFUNCTION() void HandleHost();
	UFUNCTION() void HandleJoin();
	UFUNCTION() void HandleQuit();
	UFUNCTION() void HandleIntensity();
	UFUNCTION() void HandleSettings();
	void RefreshIntensityLabels();

	void ShowStatus(const FText& Message, bool bIsError);
	FString GetEnteredName() const;

	UPROPERTY(Transient) TObjectPtr<UWidget> WarningPanel;
	UPROPERTY(Transient) TObjectPtr<UWidget> MainPanel;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> CodeBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UButton> HostButton;
	UPROPERTY(Transient) TObjectPtr<UButton> JoinButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IntensityLabelWarning;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IntensityLabelMenu;

	FDelegateHandle StatusHandle;
	double JoinBlockedUntil = 0.0;
};
