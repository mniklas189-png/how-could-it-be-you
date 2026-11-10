#pragma once

#include "CoreMinimal.h"
#include "Abilities/HCKillRules.h"
#include "Core/HCTypes.h"
#include "GameFramework/GameModeBase.h"
#include "HCGameMode.generated.h"

class AHCCharacter;
class AHCGameState;
class AHCMeetingManager;
class AHCPlayerController;
class AHCRemains;
class AHCStationBuilder;
class AHCPlayerState;
class UHCGameInstance;
class UHCMatchRules;

/**
 * Server-only rules (docs/02 §5): phase machine Lobby -> (Intro) -> RoleReveal -> Playing -> GameOver -> Lobby,
 * role assignment, win checks, join rules, spawning on the station's spawn points.
 *
 * Lobby map:  phase Lobby, host starts -> seamless travel to the match map.
 * Match map:  phase Loading until all travellers arrived, then Intro/RoleReveal.
 * Match map opened directly (PIE / quick test): phase Lobby in place; hc.StartMatch starts a round right there.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHCGameMode();

	// --- AGameModeBase ---
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void PostSeamlessTravel() override;

	// --- Requests from players (validated here) ---
	bool RequestStartMatch(APlayerController* Requester, FText& OutError);
	void RequestReturnToLobby(APlayerController* Requester);

	/** Runs a hc.* debug command on the server. Requester = nullptr when typed on the host. */
	void ExecuteDebugCommand(const FString& Command, const TArray<FString>& Args, APlayerController* Requester);

	/** Checks FHCWinConditionEvaluator and ends the match if someone won. */
	void EvaluateWinConditions(bool bMonsterJustLeft = false);

	/** Called by the task component when a player finished a whole task. */
	void NotifyTaskCompleted(AHCPlayerState* PlayerState, bool bFake);

	/** Validates a kill request (docs/02 §7.1) and starts the kill if it is allowed. */
	EHCKillReject RequestKill(AHCCharacter* Attacker, AHCCharacter* Victim);
	void NotifyJumpscareFinished(AHCPlayerController* VictimPC);
	void ReportRemains(AHCCharacter* Reporter, AHCRemains* Remains);

	/** Emergency button (docs/02 §8.1): one use per player and match, locked for a moment after every meeting. */
	void RequestEmergencyMeeting(AHCCharacter* Who);

	/** The meeting manager, created on first use (server only, not replicated). */
	AHCMeetingManager* GetMeeting();
	/** hc.KillMe: own death with jumpscare and ghost, without a monster. */
	void DebugKill(APlayerController* VictimPC);
	bool IsInMeetingRoom(const FVector& Location) const;

	bool IsLobbyMap() const;
	EHCMatchPhase GetPhase() const;
	const UHCMatchRules* GetRules() const { return Rules; }

protected:
	friend class AHCMeetingManager;

	struct FHCPendingKill
	{
		TWeakObjectPtr<AHCPlayerController> Victim;
		TWeakObjectPtr<AHCCharacter> VictimCharacter;
		TWeakObjectPtr<AHCCharacter> Attacker;
		double StartTime = 0.0;
		bool bVictimReady = false;
	};

	void BeginKill(AHCCharacter* Attacker, AHCCharacter* Victim);
	void TickPendingKills();
	void FinishKill(const FHCPendingKill& Pending);
	void MakeGhost(AHCPlayerController* PC, const FVector& Location, const FRotator& Rotation);
	void CancelKillsOf(const AController* Leaver);
	/** A player died or disconnected: a shapeshift into that player may have to end (docs/02 §6.6). */
	void NotifyShapeshifters(int32 PlayerId, bool bDisconnected);
	AHCStationBuilder* FindBuilder() const;
	double GetServerTime() const;

	TArray<FHCPendingKill> PendingKills;
	FTimerHandle KillTimer;

	void SetPhase(EHCMatchPhase NewPhase, float DurationSeconds);
	void OnPhaseTimer();
	void CheckTravellersLoaded();
	void BeginRound();
	void BeginRoleReveal();
	void BeginPlaying();
	void EndMatch(EHCWinner Winner, const FText& Reason);
	void ReturnToLobby();
	void ResetPlayersForLobby();
	int32 AssignIdentity(AHCPlayerState* PlayerState);
	void PublishLobbyInfo();
	void ListPlayers(APlayerController* Requester) const;
	void AssignTasks(const TArray<AHCPlayerState*>& Players, int32 Seed);
	/** Writes the task progress into the game state as far as the lobby option allows (docs/02 §9.3). */
	void PublishTaskProgress(bool bFinal = false);

	AHCGameState* GetHCGameState() const;
	UHCGameInstance* GetHCGameInstance() const;
	TArray<AHCPlayerState*> GetHCPlayerStates() const;

	UPROPERTY(Transient)
	TObjectPtr<UHCMatchRules> Rules;

	/** The real numbers live only on the server; the game state gets what the bar may show. */
	int32 TrueCompletedTasks = 0;
	int32 TrueTotalTasks = 0;
	int32 PlayersAtRoundStart = 0;

	UPROPERTY(Transient)
	TObjectPtr<AHCMeetingManager> Meeting;

	/** The monster was voted out in this round (reason text of the win). */
	bool bMonsterEjected = false;

	FTimerHandle PhaseTimer;
	FTimerHandle LoadingTimer;
	double LoadingStartedAt = 0.0;
	int32 ExpectedTravellers = 0;
	bool bPlayIntro = false;
	/** True if this match map was reached from the lobby (not opened directly for a quick test). */
	bool bCameFromLobby = false;
};
