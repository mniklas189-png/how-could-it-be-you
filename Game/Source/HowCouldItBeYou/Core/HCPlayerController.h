#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HCPlayerController.generated.h"

class AHCCharacter;
class AHCRemains;
class AHCTaskStation;
class UHCInputConfig;

UCLASS()
class HOWCOULDITBEYOU_API AHCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHCPlayerController();

	/** Created on first use; holds the Enhanced Input actions and the default mapping context. */
	UHCInputConfig* GetInputConfig();

	// --- Requests to the server (the only "mailbox" for player requests, docs/02 §4) ---
	UFUNCTION(Server, Reliable)
	void ServerRequestStartMatch();

	UFUNCTION(Server, Reliable)
	void ServerRequestReturnToLobby();

	UFUNCTION(Server, Reliable)
	void ServerDebugCommand(const FString& Command, const TArray<FString>& Args);

	// --- Tasks (docs/02 §9.4) ---
	/** Server -> owner: open the overlay of a Sequence/Minigame step. */
	UFUNCTION(Client, Reliable)
	void ClientOpenTaskOverlay(AHCTaskStation* Station);

	/** Owner -> server: the overlay step is solved; the server checks duration and distance. */
	UFUNCTION(Server, Reliable)
	void ServerCompleteTaskStep(AHCTaskStation* Station);

	UFUNCTION(Server, Reliable)
	void ServerCancelTaskStep();

	// --- Kill and ghosts (docs/02 §7) ---
	/** Server -> victim only: start the jumpscare. */
	UFUNCTION(Client, Reliable)
	void ClientStartJumpscare(FVector_NetQuantize AttackerLocation);

	/** Victim -> server: the screen is black, the ghost can take over. */
	UFUNCTION(Server, Reliable)
	void ServerJumpscareFinished();

	/** Server -> new ghost only: who the Wandler is (for the red marker). */
	UFUNCTION(Client, Reliable)
	void ClientRevealMonstersToGhost(const TArray<int32>& MonsterPlayerIds);

	/** Server -> attacker: turn towards the victim (only the rotation). */
	UFUNCTION(Client, Reliable)
	void ClientFaceLocation(FVector_NetQuantize Location);

	bool IsMonsterRevealed(int32 PlayerId) const;

	/** hc.Jumpscare: watch the jumpscare without dying (local only). */
	void StartJumpscarePreview();

	/** Server -> this player: short error, e.g. "Nur der Host kann die Runde starten." */
	UFUNCTION(Client, Reliable)
	void ClientShowMessage(const FText& Message);

	/** Last message from ClientShowMessage (empty after a few seconds). */
	FText GetClientMessage() const;

	/** Is this the host's own controller (listen server)? */
	bool IsHost() const;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* P) override;

	void ApplyMappingContext();
	void Input_Pause();
	void Input_LobbyMenu();

	TArray<int32> RevealedMonsterIds;

	FText ClientMessage;
	double ClientMessageUntil = 0.0;

	/**
	 * Smoke test for automated checks: start with -HCShots=N [-HCShotDir=<path>].
	 * Turns the flashlight on, takes N screenshots while turning 90 degrees each, walks forward, then quits.
	 */
	void StartSmokeTestIfRequested();
	void SmokeTestStep();

	UPROPERTY(Transient)
	TObjectPtr<UHCInputConfig> InputConfig;

	/**
	 * Round smoke test (Development builds): -HCPhaseShots (screenshot per phase), -HCAutoStart=<s> and
	 * -HCAutoEnd=<s> (host starts/ends the round), -HCQuitAfterRound (quit once back in the lobby).
	 */
	void TickRoundSmokeTest();
	/** -HCAutoTasks: walks through the own task list (teleport to the station, interact, wait). */
	void TickAutoTasks(double Now);
	bool TickAutoKill(double Now);
	bool TickAutoReport(double Now);
	void TickGhostCheck(double Now);
	bool bSmokeAutoKill = false;
	bool bSmokeAutoReport = false;
	bool bSmokeGhostCheck = false;
	float SmokeAutoKillMe = 0.f;
	bool bSmokeKillMeDone = false;
	int32 SmokeKillState = 0;
	TWeakObjectPtr<AHCCharacter> SmokeKillTarget;
	double SmokeKillSince = 0.0;
	TWeakObjectPtr<AHCRemains> SmokeReportTarget;
	TSet<TWeakObjectPtr<AHCRemains>> SmokeReportTried;
	double SmokeReportSince = 0.0;
	bool bSmokeReportPressed = false;
	double SmokeGhostCheckNext = 0.0;
	double SmokeDeadSince = 0.0;
	bool bSmokeGhostShot = false;
	bool bSmokeMonsterShot = false;
	bool bSmokeAutoTasks = false;
	int32 SmokeTaskState = 0;
	TWeakObjectPtr<AHCTaskStation> SmokeTaskStation;
	FName SmokeTaskId;
	int32 SmokeTaskStep = 0;
	double SmokeTaskSince = 0.0;
	double SmokeTaskNextTime = 0.0;
	double SmokeTaskInteractAt = 0.0;
	bool bSmokeOverlayShot = false;
	double SmokeTaskIdleSince = 0.0;
	bool bSmokeTasksDoneLogged = false;
	bool bSmokePhaseShots = false;
	bool bSmokeQuitAfterRound = false;
	float SmokeAutoStart = 0.f;
	float SmokeAutoEnd = 0.f;
	bool bSmokeRoundFlags = false;
	uint8 SmokeLastPhase = 255;
	double SmokePhaseSince = 0.0;
	bool bSmokePhaseShotTaken = false;

	int32 SmokeShotsLeft = 0;
	int32 SmokeShotIndex = 0;
	int32 SmokeWalkTicks = 0;
	bool bSmokeWalking = false;
	FString SmokeShotDir;
	FTimerHandle SmokeTimer;
};
