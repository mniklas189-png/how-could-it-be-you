#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HCTypes.h"
#include "HCMeetingWidget.generated.h"

class AHCGameState;
class AHCPlayerState;
class UEditableTextBox;
class UHCPaintBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetTree;
struct FHCChatLine;
struct FHCDraw;
struct FHCPaintState;

/**
 * Meeting screen (docs/02 §8) in the game's own look: the stage with its countdown on top, one card per player
 * (suit colour, symbol, name, what they are doing), below it the ballot. Voting takes two steps, because a vote is
 * final: pick a card (or "nobody"), then confirm. Afterwards the cards show who voted for whom. On the right the
 * chat of the round: Enter starts typing, Enter sends, Escape leaves the box. Ghosts write only to ghosts.
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
	void HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleChatChanged(const FText& Text);

	bool CanVoteNow(const AHCGameState* GS) const;
	/** The ballot is open for this player: voting runs and no vote of theirs is on its way to the server. */
	bool CanPickNow() const;
	/** This player's card can be picked: alive, somebody else, and the ballot is open. */
	bool CanPickPlayer(int32 PlayerId) const;
	const AHCPlayerState* FindPlayer(const AHCGameState* GS, int32 PlayerId) const;
	/** Changes only when players come or go: the cards read everything else while they are painted. */
	uint32 ComputeSignature(const AHCGameState* GS) const;
	void Rebuild(const AHCGameState* GS);

	/** A card or the "nobody" line was clicked: marks it, a second click takes the mark back. */
	void PickTarget(int32 TargetPlayerId);
	/** The marked choice becomes the vote. */
	void ConfirmVote();

	void PaintBackdrop(const FHCDraw& D) const;
	void PaintHeader(const FHCDraw& D) const;
	void PaintCard(const FHCDraw& D, const FHCPaintState& S, int32 PlayerId, int32 CardIndex) const;
	void PaintSkipLine(const FHCDraw& D, const FHCPaintState& S) const;
	void PaintConfirm(const FHCDraw& D, const FHCPaintState& S) const;
	/** The line under the cards: what to do now, or the verdict. */
	void PaintInfo(const FHCDraw& D) const;
	/** Small squares in the suit colours of everyone who voted for this target, appearing one after the other. Returns the votes. */
	int32 PaintVotes(const FHCDraw& D, const AHCGameState* GS, int32 TargetPlayerId, double RightX, double CenterY) const;

	UWidget* BuildChat(UWidgetTree* Tree);
	/** Adds the lines that arrived since the last frame. */
	void UpdateChat(const AHCPlayerState* Own);
	void AddChatLine(const FHCChatLine& Line, const AHCPlayerState* Own);
	bool TrySendChat(const FString& Text);
	bool IsTypingChat() const;
	/** Keys (Enter, Escape) must reach this screen, also after a click on a card. */
	void KeepKeyboardFocus();

	UPROPERTY(Transient) TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChatHint;
	int32 ShownChatEpoch = -1;
	int32 ShownChatSerial = 0;
	double LastChatSent = -1000.0;

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerGrid;
	UPROPERTY(Transient) TObjectPtr<UHCPaintBox> SkipLine;
	UPROPERTY(Transient) TObjectPtr<UHCPaintBox> ConfirmButton;
	uint32 ShownSignature = 0;

	// The ballot
	bool bHasPick = false;
	int32 PickedTarget = -1;
	/** What this player voted for in the running vote (shown on the card). */
	bool bHasOwnVote = false;
	int32 OwnVoteTarget = -1;
	double VoteSentAt = -1000.0;

	// For the animations
	double OpenedAt = 0.0;
	EHCMeetingStage ShownStage = EHCMeetingStage::None;
	double StageSince = 0.0;
	/** Longest countdown seen in this stage = its full length (the rules stay on the server). */
	float StageTotal = 0.f;
	/** The info line under the ballot, set every tick. */
	FText InfoLine;
	FLinearColor InfoColor = FLinearColor::White;
};
