#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HCTypes.h"
#include "HCLobbyWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UWidget;

/** Lobby panel: join code, LAN address, map and voice chat rule (chosen by the host, seen by all), player list, start
 *  (host only), walk around, leave (docs/02 §16.2). The host has a second panel on the right with the settings of the
 *  round: Wandler tasks, votes, vents, a second Wandler, jobs. The others do not see it. */
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
	/** Tasks of the Wandler: a fake list or none; only the host can click, everybody sees the choice. */
	UFUNCTION() void HandleMonsterTasksOn();
	UFUNCTION() void HandleMonsterTasksOff();
	void SetMonsterHasTasks(bool bHasTasks);
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> MonsterTasksButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MonsterTasksInfo;
	/** Votes in the meeting: open (who voted for whom) or anonymous (only the counts); only the host can click. */
	UFUNCTION() void HandleVotesOpen();
	UFUNCTION() void HandleVotesAnonymous();
	void SetAnonymousVotes(bool bAnonymous);
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> VotesButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VotesInfo;
	/** Map of the next round: one button per entry of HCMaps::All(); only the host can click. */
	UFUNCTION() void HandleMap0();
	UFUNCTION() void HandleMap1();
	UFUNCTION() void HandleMap2();
	void SetStationMap(int32 MapIndex);
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapTitle;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> MapButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapInfo;
	/** Settings of the round (Core/HCRoundOptions.h), host only: pairs of buttons on / off. */
	UFUNCTION() void HandleVentsOn();
	UFUNCTION() void HandleVentsOff();
	UFUNCTION() void HandleSecondOn();
	UFUNCTION() void HandleSecondOff();
	UFUNCTION() void HandleJobsOn();
	UFUNCTION() void HandleJobsOff();
	/** One button per job (HCJobs::FromIndex), each switches its job on or off. */
	UFUNCTION() void HandleJob0();
	UFUNCTION() void HandleJob1();
	UFUNCTION() void HandleJob2();
	UFUNCTION() void HandleJob3();
	UFUNCTION() void HandleJob4();
	UFUNCTION() void HandleJob5();
	void ToggleJob(int32 Index);
	void RefreshSettings(const class AHCGameState* GS, int32 PlayerCount);
	UPROPERTY(Transient) TObjectPtr<UWidget> SettingsPanel;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> VentsButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VentsInfo;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SecondButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecondInfo;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> JobsButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> JobButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JobsInfo;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PlayerList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;

	float RefreshTimer = 0.f;
	FString LastPlayerKey;
};
