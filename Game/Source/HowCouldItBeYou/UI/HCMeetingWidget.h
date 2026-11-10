#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCMeetingWidget.generated.h"

class AHCGameState;
class UButton;
class UHCMeetingWidget;
class UTextBlock;
class UVerticalBox;

/** Carries the target of one "vote" button (UButton::OnClicked has no parameters). */
UCLASS()
class HOWCOULDITBEYOU_API UHCVoteClickProxy : public UObject
{
	GENERATED_BODY()

public:
	int32 TargetPlayerId = -1;
	TWeakObjectPtr<UHCMeetingWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Meeting screen (docs/02 §8, prototype look): who called it and why, stage and countdown, one row per player
 * with the vote button, afterwards who voted for whom and the result.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCMeetingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sends the own vote (-1 = skip). False if voting is not possible right now. Also used by the test bots. */
	bool SubmitVote(int32 TargetPlayerId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleSkip();

	bool CanVoteNow(const AHCGameState* GS) const;
	uint32 ComputeSignature(const AHCGameState* GS) const;
	void Rebuild(const AHCGameState* GS);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> CallerText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StageText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TaskText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerList;
	UPROPERTY(Transient) TObjectPtr<UButton> SkipButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UHCVoteClickProxy>> Proxies;

	uint32 ShownSignature = 0;
};
