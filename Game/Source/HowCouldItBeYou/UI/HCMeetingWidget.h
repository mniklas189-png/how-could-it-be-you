#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCMeetingWidget.generated.h"

class AHCGameState;
class AHCPlayerState;
class UButton;
class UEditableTextBox;
class UHCMeetingWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetTree;
struct FHCChatLine;

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
 * with the vote button, afterwards who voted for whom and the result. On the right the chat of the round:
 * Enter starts typing, Enter sends, Escape leaves the box. Ghosts write only to ghosts.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCMeetingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sends the own vote (-1 = skip). False if voting is not possible right now. Also used by the test bots. */
	bool SubmitVote(int32 TargetPlayerId);

	/** Types the text into the chat box and sends it like Enter does. False if it was not sent. Also used by the test bots. */
	bool SendChatText(const FString& Text);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleSkip();

	UFUNCTION()
	void HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleChatChanged(const FText& Text);

	bool CanVoteNow(const AHCGameState* GS) const;
	uint32 ComputeSignature(const AHCGameState* GS) const;
	void Rebuild(const AHCGameState* GS);

	UWidget* BuildChat(UWidgetTree* Tree);
	/** Adds the lines that arrived since the last frame. */
	void UpdateChat(const AHCPlayerState* Own);
	void AddChatLine(const FHCChatLine& Line, const AHCPlayerState* Own);
	bool TrySendChat(const FString& Text);
	bool IsTypingChat() const;
	/** Keys (Enter, Escape) must reach this screen, also after a clicked vote button disappeared. */
	void KeepKeyboardFocus();

	UPROPERTY(Transient) TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChatHint;
	int32 ShownChatEpoch = -1;
	int32 ShownChatSerial = 0;
	double LastChatSent = -1000.0;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> CallerText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StageText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TaskText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerList;
	UPROPERTY(Transient) TObjectPtr<UButton> SkipButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UHCVoteClickProxy>> Proxies;

	uint32 ShownSignature = 0;
};
