#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/GameStateBase.h"
#include "HCGameState.generated.h"

/**
 * The scoreboard everyone sees (docs/02 §4): phase, countdown end time, result, revealed roles,
 * lobby info. Only AHCGameMode changes it.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	EHCMatchPhase GetMatchPhase() const { return MatchPhase; }

	/** Seconds left in the current phase (computed locally from the replicated end time), 0 = no countdown. */
	float GetPhaseRemainingSeconds() const;

	/** Server only. Duration <= 0 means no countdown. */
	void ServerSetPhase(EHCMatchPhase NewPhase, float DurationSeconds);
	void ServerSetPhaseEnd(float SecondsFromNow);

	void ServerSetResult(EHCWinner InWinner, const FText& InReason, const TArray<FHCRevealedRole>& InRoles);
	void ServerSetLobbyInfo(const FString& InJoinCode, const FString& InHostAddress, const FString& InModeName, bool bInOnlineLobby);

	/** Serial of the current role assignment; the HUD shows the role once the own RoleInfo carries it. */
	uint8 GetMatchSerial() const { return MatchSerial; }
	void ServerSetMatchSerial(uint8 InSerial) { MatchSerial = InSerial; }

	/** How many players want to skip the story at the start of the round. */
	int32 GetIntroSkipVotes() const { return IntroSkipVotes; }
	void ServerSetIntroSkipVotes(int32 InVotes) { IntroSkipVotes = static_cast<uint8>(FMath::Clamp(InVotes, 0, 255)); }

	/** Task progress for the bar. Only what the lobby option allows is written here (docs/02 §9.3). */
	int32 GetCompletedTasks() const { return CompletedTasks; }
	int32 GetTotalTasks() const { return TotalTasks; }
	bool IsTaskBarVisible() const { return bTaskBarVisible; }
	void ServerSetTaskProgress(int32 InCompleted, int32 InTotal, bool bInVisible);

	EHCWinner GetWinner() const { return Winner; }
	const FText& GetWinReason() const { return WinReason; }
	const TArray<FHCRevealedRole>& GetRevealedRoles() const { return RevealedRoles; }
	const FString& GetJoinCode() const { return JoinCode; }
	const FString& GetHostAddress() const { return HostAddress; }
	/** The lobby can also be joined over the internet (EOS), not only in the same network. */
	bool IsOnlineLobby() const { return bOnlineLobby; }
	const FString& GetModeName() const { return ModeName; }

	/** When the players hear each other (lobby option of the host, Voice/HCVoiceRules.h). */
	EHCVoiceChatRule GetVoiceChatRule() const { return VoiceChatRule; }
	void ServerSetVoiceChatRule(EHCVoiceChatRule InRule) { VoiceChatRule = InRule; }

	/** Lobby option of the host: off = the Wandler gets no task list at all (otherwise a fake one that never counts). */
	bool DoesMonsterHaveTasks() const { return bMonsterHasTasks; }
	void ServerSetMonsterHasTasks(bool bInHasTasks) { bMonsterHasTasks = bInHasTasks; }

	/** Lobby option of the host: on = the results show only how many votes everyone got, not who voted for whom. */
	bool AreVotesAnonymous() const { return bAnonymousVotes; }
	void ServerSetAnonymousVotes(bool bInAnonymous) { bAnonymousVotes = bInAnonymous; }

	/** Lobby options of the host (Core/HCRoundOptions.h): ventilation shafts, a second Wandler from 8 players, jobs. */
	bool AreVentsEnabled() const { return bVentsEnabled; }
	bool IsSecondMonsterEnabled() const { return bSecondMonster; }
	bool AreJobsEnabled() const { return bJobsEnabled; }
	/** Jobs switched on (HCJobs::Bit). */
	uint8 GetJobMask() const { return JobMask; }
	void ServerSetRoundOptions(bool bInVents, bool bInSecondMonster, bool bInJobs, uint8 InJobMask)
	{
		bVentsEnabled = bInVents;
		bSecondMonster = bInSecondMonster;
		bJobsEnabled = bInJobs;
		JobMask = InJobMask;
	}

	// --- Crisis sabotage (meltdown, oxygen): everybody sees it and its countdown ---
	EHCCrisis GetCrisis() const { return Crisis; }
	/** Seconds until the crisis is lost (0 without one). */
	float GetCrisisRemaining() const;
	/** How many of the two panels are held right now. */
	int32 GetCrisisPanelsHeld() const { return CrisisPanelsHeld; }
	void ServerSetCrisis(EHCCrisis InCrisis, double InEndServerTime) { Crisis = InCrisis; CrisisEndServerTime = InEndServerTime; CrisisPanelsHeld = 0; }
	void ServerSetCrisisPanelsHeld(int32 InHeld) { CrisisPanelsHeld = static_cast<uint8>(FMath::Clamp(InHeld, 0, 2)); }
	/** 0..1: how long both panels have been held together, of the time that ends the crisis. */
	float GetCrisisFix() const { return CrisisFix / 100.f; }
	void ServerSetCrisisFix(float Fraction) { CrisisFix = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Fraction * 100.f), 0, 100)); }

	/** Lobby option of the host: which map the next round is played on (index into HCMaps::All()). */
	int32 GetStationMap() const { return StationMap; }
	void ServerSetStationMap(int32 InMap) { StationMap = static_cast<uint8>(FMath::Clamp(InMap, 0, 255)); }

	// --- Meeting (docs/02 §8); written by AHCMeetingManager ---
	EHCMeetingStage GetMeetingStage() const { return MeetingStage; }
	int32 GetMeetingCallerId() const { return MeetingCallerId; }
	const FString& GetMeetingCallerName() const { return MeetingCallerName; }
	/** Empty = emergency button, otherwise the name on the reported remains. */
	const FString& GetMeetingVictimName() const { return MeetingVictimName; }
	const TArray<FHCVoteResultEntry>& GetVoteResults() const { return VoteResults; }
	int32 GetEjectedPlayerId() const { return EjectedPlayerId; }
	/** Result line of the vote, later the text of the ejection screen. */
	const FText& GetMeetingText() const { return MeetingText; }
	/** Server time from which the emergency button works again. */
	double GetEmergencyReadyServerTime() const { return EmergencyReadyServerTime; }

	void ServerSetMeetingStage(EHCMeetingStage InStage) { MeetingStage = InStage; }
	void ServerSetMeetingInfo(int32 InCallerId, const FString& InCallerName, const FString& InVictimName)
	{
		MeetingCallerId = InCallerId;
		MeetingCallerName = InCallerName;
		MeetingVictimName = InVictimName;
	}
	void ServerSetVoteResults(const TArray<FHCVoteResultEntry>& InResults, int32 InEjectedId, const FText& InText)
	{
		VoteResults = InResults;
		EjectedPlayerId = InEjectedId;
		MeetingText = InText;
	}
	void ServerSetMeetingText(const FText& InText) { MeetingText = InText; }
	void ServerSetEmergencyReadyTime(double InServerTime) { EmergencyReadyServerTime = InServerTime; }

	/** Short message for everyone, e.g. "Lena hat die Verbindung verloren." */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnouncement(const FText& Text, float Seconds);

	/** Host is ending the session; clients go to the main menu with a message (docs/03 §7.6). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHostEnded();

	/** Sound, short glow and slime trail of a shapeshift. Only the place is sent, never who it was (docs/02 §6.4). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayShapeshiftFX(FVector_NetQuantize FloorLocation);

	/** Sabotage announcement for everyone: text line and alarm tone, never who did it (docs/01 §7 rule 6). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSabotageAlert(const FText& Text);

	/** Local: current announcement (empty if expired). */
	FText GetAnnouncement() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_MatchPhase();

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase)
	EHCMatchPhase MatchPhase = EHCMatchPhase::Lobby;

	UPROPERTY(Replicated)
	double PhaseStartServerTime = 0.0;

	UPROPERTY(Replicated)
	double PhaseEndServerTime = 0.0;

	UPROPERTY(Replicated)
	uint8 MatchSerial = 0;

	UPROPERTY(Replicated)
	uint8 IntroSkipVotes = 0;

	UPROPERTY(Replicated)
	int32 CompletedTasks = 0;

	UPROPERTY(Replicated)
	int32 TotalTasks = 0;

	UPROPERTY(Replicated)
	bool bTaskBarVisible = false;

	UPROPERTY(Replicated)
	EHCWinner Winner = EHCWinner::None;

	UPROPERTY(Replicated)
	FText WinReason;

	UPROPERTY(Replicated)
	TArray<FHCRevealedRole> RevealedRoles;

	UPROPERTY(Replicated)
	FString JoinCode;

	UPROPERTY(Replicated)
	FString HostAddress;

	UPROPERTY(Replicated)
	bool bOnlineLobby = false;

	UPROPERTY(Replicated)
	FString ModeName;

	UPROPERTY(Replicated)
	EHCVoiceChatRule VoiceChatRule = EHCVoiceChatRule::Proximity;

	UPROPERTY(Replicated)
	bool bMonsterHasTasks = true;

	UPROPERTY(Replicated)
	bool bAnonymousVotes = false;

	UPROPERTY(Replicated)
	uint8 StationMap = 0;

	UPROPERTY(Replicated)
	bool bVentsEnabled = true;

	UPROPERTY(Replicated)
	bool bSecondMonster = true;

	UPROPERTY(Replicated)
	bool bJobsEnabled = true;

	UPROPERTY(Replicated)
	uint8 JobMask = 0x7E;

	UPROPERTY(Replicated)
	EHCCrisis Crisis = EHCCrisis::None;

	UPROPERTY(Replicated)
	double CrisisEndServerTime = 0.0;

	UPROPERTY(Replicated)
	uint8 CrisisPanelsHeld = 0;

	UPROPERTY(Replicated)
	uint8 CrisisFix = 0;

	UPROPERTY(Replicated)
	EHCMeetingStage MeetingStage = EHCMeetingStage::None;

	UPROPERTY(Replicated)
	int32 MeetingCallerId = INDEX_NONE;

	UPROPERTY(Replicated)
	FString MeetingCallerName;

	UPROPERTY(Replicated)
	FString MeetingVictimName;

	UPROPERTY(Replicated)
	TArray<FHCVoteResultEntry> VoteResults;

	UPROPERTY(Replicated)
	int32 EjectedPlayerId = INDEX_NONE;

	UPROPERTY(Replicated)
	FText MeetingText;

	UPROPERTY(Replicated)
	double EmergencyReadyServerTime = 0.0;

	FText Announcement;
	double AnnouncementUntil = 0.0;
};
