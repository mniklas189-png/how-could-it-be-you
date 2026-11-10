#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCPauseWidget.generated.h"

class UTextBlock;

/** Esc overlay - no real pause in multiplayer (docs/02 §16.2): continue, "I'm stuck", leave (host confirms). */
UCLASS()
class HOWCOULDITBEYOU_API UHCPauseWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ResetConfirm();

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION() void HandleContinue();
	UFUNCTION() void HandleUnstuck();
	UFUNCTION() void HandleLeave();
	UFUNCTION() void HandleIntensity();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> LeaveLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IntensityLabel;

	bool bConfirmLeave = false;
	double LastUnstuckTime = -1000.0;
};
