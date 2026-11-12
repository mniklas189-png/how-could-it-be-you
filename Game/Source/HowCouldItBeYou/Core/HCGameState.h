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
	void ServerSetLobbyInfo(const FString& InJoinCode, const FString& InHostAddress, const FString& InModeName);

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
	const FString& GetModeName() const { return ModeName; }

	/** When the players hear each other (lobby option of the host, Voice/HCVoiceRules.h). */
	EHCVoiceChatRule GetVoiceChatRule() const { return VoiceChatRule; }
	void ServerSetVoiceChatRule(EHCVoiceChatRule InRule) { VoiceChatRule = InRule; }

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
	FString ModeName;

	UPROPERTY(Replicated)
	EHCVoiceChatRule VoiceChatRule = EHCVoiceChatRule::Proximity;

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
