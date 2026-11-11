#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCSettingsWidget.generated.h"

class APlayerController;
class UHCSettingsWidget;
class UTextBlock;

/** A button of the settings menu: UButton::OnClicked has no parameters, so each button gets one of these. */
UCLASS()
class HOWCOULDITBEYOU_API UHCSettingsClickProxy : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UHCSettingsWidget> Owner;

	int32 Row = 0;
	int32 Delta = 0;

	UFUNCTION()
	void HandleClick();
};

/**
 * Personal settings: volume, mouse, field of view, graphics, window, shock effects. Opened from the main menu and
 * from the pause menu, lies on top of them. Every change takes effect at once and is saved.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	enum ERow : int32 { Volume, Mouse, Fov, Quality, Display, VSync, Shock, RowCount };

	/** Opens the settings on top of whatever this player sees. */
	static UHCSettingsWidget* Open(APlayerController* PC);

	/** One step down (-1) or up (+1) in a row. */
	void Adjust(int32 Row, int32 Delta);

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleClose();

	void Refresh();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Values;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHCSettingsClickProxy>> Proxies;
};
