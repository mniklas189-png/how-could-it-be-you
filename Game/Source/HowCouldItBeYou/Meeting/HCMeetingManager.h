#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/Info.h"
#include "HCMeetingManager.generated.h"

class AHCGameMode;
class AHCGameState;
class AHCPlayerState;

/**
 * Runs a meeting from the trigger to the ejection (docs/02 §8). Exists only on the server and is not replicated:
 * what players may see goes into AHCGameState (stage, end time, caller, results), requests come in through
 * AHCPlayerController.
 *
 * Gathering -> Discussion -> Voting -> Results -> phase Ejection (if someone is ejected) -> back to Playing.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCMeetingManager : public AInfo
{
	GENERATED_BODY()

public:
	AHCMeetingManager();

	void Init(AHCGameMode* InGameMode);

	/**
	 * Starts a meeting. VictimName empty = emergency button, otherwise remains were reported.
	 * False if a meeting is already on its way or the round is not in play - the first trigger wins (docs/02 §8.1).
	 */
	bool RequestMeeting(const AHCPlayerState* Caller, const FString& VictimName);

	/** One final vote per living player; TargetPlayerId = FHCVoteTally::SkipId skips. False + reason if rejected. */
	bool CastVote(AHCPlayerState* Voter, int32 TargetPlayerId, FText& OutError);

	/** A player disconnected: the vote is removed, votes for that player are handed back (docs/02 §8.8). */
	void NotifyPlayerLeft(int32 PlayerId);

	/** The ejection screen is over (phase timer of the game mode). */
	void OnEjectionFinished();

	/** The match ended in the middle of a meeting. */
	void Abort();

	/** New round: emergency uses back to full, no cooldown, nothing pending. */
	void ResetForRound();

	/** A trigger was accepted and waits for a running kill, or a meeting is running. */
	bool IsBusy() const { return bPending || Stage != EHCMeetingStage::None; }
	EHCMeetingStage GetStage() const { return Stage; }
	double GetEmergencyReadyTime() const { return EmergencyReadyTime; }

private:
	void TickWaitForKills();
	void BeginGathering();
	void BeginVoting();
	void BeginResults();
	void AfterResults();
	void BeginEjection(AHCPlayerState* Ejected);
	void ResumePlaying();
	void OnStageTimer();
	void SetStage(EHCMeetingStage NewStage, float Duration);
	void SeatPlayers();
	/** Living, connected players: they vote and can be voted for. */
	TArray<AHCPlayerState*> GetEligible() const;
	bool HaveAllVoted() const;
	AHCPlayerState* FindPlayer(int32 PlayerId) const;
	AHCGameState* GetHCGameState() const;

	UPROPERTY(Transient)
	TObjectPtr<AHCGameMode> GameMode;

	EHCMeetingStage Stage = EHCMeetingStage::None;
	bool bPending = false;
	double PendingSince = 0.0;
	int32 CallerId = INDEX_NONE;
	FString CallerName;
	FString VictimName;
	/** Voter -> target; only the server knows it until the results (docs/02 §8.4). */
	TMap<int32, int32> Votes;
	int32 EjectedId = INDEX_NONE;
	double EmergencyReadyTime = 0.0;
	FTimerHandle StageTimer;
	FTimerHandle WaitTimer;
};
