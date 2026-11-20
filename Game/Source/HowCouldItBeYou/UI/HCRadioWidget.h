#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCRadioWidget.generated.h"

class UEditableTextBox;
struct FHCDraw;
struct FHCPaintState;

/**
 * The radio operator's set (ability key): once between two meetings either an announcement to everybody - one line,
 * with the own name and the room it came from - or an emergency meeting from wherever one stands (it uses up the own
 * emergency meeting). What is said may be a lie; nobody can check it.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCRadioWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** For the test bots: sends an announcement and closes. */
	void SendAnnouncement(const FString& Text);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void PaintPanel(const FHCDraw& D, const FHCPaintState& S) const;
	void CallMeeting();
	void Close();
	bool HasCharge() const;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> MessageBox;
};
