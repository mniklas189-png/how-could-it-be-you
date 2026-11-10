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

	/** Short message for everyone, e.g. "Lena hat die Verbindung verloren." */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnouncement(const FText& Text, float Seconds);

	/** Host is ending the session; clients go to the main menu with a message (docs/03 §7.6). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHostEnded();

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

	FText Announcement;
	double AnnouncementUntil = 0.0;
};
