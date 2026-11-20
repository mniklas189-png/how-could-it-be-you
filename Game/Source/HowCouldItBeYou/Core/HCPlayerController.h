#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Meeting/HCChat.h"
#include "HCPlayerController.generated.h"

class AHCCharacter;
class AHCRemains;
class AHCTaskStation;
class AHCMonitorDevice;
class AHCWardrobe;
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

	/** Held the interact key during the story: this player wants to skip it (the majority decides). */
	UFUNCTION(Server, Reliable)
	void ServerVoteSkipIntro();

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

	// --- Camera monitors and admin plan (World/HCMonitorDevices) ---
	/** Server -> owner: the player may look now, open the view. */
	UFUNCTION(Client, Reliable)
	void ClientOpenDeviceView(AHCMonitorDevice* Device);

	/** Server -> owner: the server ended the looking (pushed away, power cut). */
	UFUNCTION(Client, Reliable)
	void ClientCloseDeviceView();

	/** Owner -> server: the player closed the view. */
	UFUNCTION(Server, Reliable)
	void ServerCloseDeviceView(AHCMonitorDevice* Device);

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

	// --- Meeting (docs/02 §8) ---
	/** Owner -> server: the one, final vote of this vote; -1 = skip. */
	UFUNCTION(Server, Reliable)
	void ServerCastVote(int32 TargetPlayerId);

	/** Owner -> server: one line in the meeting chat; the server checks meeting, length and pace. */
	UFUNCTION(Server, Reliable)
	void ServerSendChat(const FString& Text);

	/** Server -> this player: a chat line this player may read (ghost lines reach only ghosts). */
	UFUNCTION(Client, Reliable)
	void ClientReceiveChat(const FHCChatLine& Line);

	/** Chat of the current round on this machine, oldest first, at most HCChat::MaxLines. */
	const TArray<FHCChatLine>& GetChatLines() const { return ChatLines; }
	/** Changes whenever the chat starts over (a new round). */
	int32 GetChatEpoch() const { return ChatEpoch; }

	// --- Sabotage (docs/02 §11.1) ---
	/** Wandler -> server. Type = EHCSabotageType; target = sector id or door id. */
	UFUNCTION(Server, Reliable)
	void ServerRequestSabotage(uint8 SabotageType, FName TargetId);

	bool IsMonsterRevealed(int32 PlayerId) const;

	/** hc.Jumpscare [1-4]: watch the jumpscare without dying (local only); 0 = a random one. */
	void StartJumpscarePreview(int32 Style = 0);

	// --- Jobs (Roles/HCJobManager.h) ---
	/** Owner -> server: ability key (Action = EHCAbilityAction); the radio operator's text comes along. */
	UFUNCTION(Server, Reliable)
	void ServerUseAbility(uint8 Action, const FString& Text);

	/** Server -> this player: a longer notice in the middle of the screen (the medic's result). */
	UFUNCTION(Client, Reliable)
	void ClientShowNotice(const FText& Text, float Seconds);

	// --- Wardrobe in the lobby (World/HCWardrobe.h) ---
	/** Server -> the player at the wardrobe: open the wardrobe window. */
	UFUNCTION(Client, Reliable)
	void ClientOpenWardrobe(AHCWardrobe* Wardrobe);

	/** Owner -> server: colour (identity, unique) and the small things (HCOutfit byte). Lobby only. */
	UFUNCTION(Server, Reliable)
	void ServerSetLook(int32 InIdentity, uint8 InOutfit);

	/** The look chosen at the wardrobe, remembered on this PC (INDEX_NONE = never chosen). */
	static void SaveLook(const UObject* WorldContext, int32 InIdentity, uint8 InOutfit);
	static bool LoadLook(const UObject* WorldContext, int32& OutIdentity, uint8& OutOutfit);

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

	TArray<FHCChatLine> ChatLines;
	int32 ChatLineSerial = 0;
	int32 ChatEpoch = 0;

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
	/** -HCAutoButton=<s>: press the emergency button after <s> seconds of play. */
	bool TickAutoButton(double Now, double InPhase);
	/** -HCAutoVote=<Name|skip|none>: vote in every meeting; screenshots of the voting and the result. */
	void TickAutoMeeting(double Now);
	/**
	 * -HCAutoShift: as the Wandler shapeshift whenever possible (through the menu); every bot logs what each
	 * figure looks like and crew bots take a picture of a disguised figure. -HCShiftRevert=<s> ends it early with Q.
	 */
	bool TickAutoShift(double Now);
	/** -HCAutoSabotage: as the Wandler cut the power and lock doors in turn, through the station map. */
	bool TickAutoSabotage(double Now);
	/** -HCAutoRepair: go to the fuse box of a dark sector and repair. All bots: picture and log of an outage. */
	bool TickAutoRepair(double Now);
	/** -HCDeviceShots (host): look at the camera monitors (grid and one large) and the admin plan, a picture of each. */
	bool TickAutoDevices(double Now, double InPhase);
	/** -HCMapShots: open the station map (M) early in the round and again during a power cut, a picture of each. */
	bool TickAutoMapShots(double Now, double InPhase);
	/**
	 * -HCAutoVent: as the Wandler climb into a ventilation shaft, crawl to the next hatch and climb out, a picture of
	 * each step; a crew bot checks that the hatch offers it nothing.
	 */
	bool TickAutoVents(double Now, double InPhase);
	int32 SmokeVentState = 0;
	double SmokeVentSince = 0.0;
	TWeakObjectPtr<class AHCVent> SmokeVentFirst;
	int32 SmokeMapShotState = 0;
	double SmokeMapShotSince = 0.0;
	int32 SmokeDeviceState = 0;
	double SmokeDeviceSince = 0.0;
	bool bSmokeAutoSabotage = false;
	bool bSmokeAutoRepair = false;
	int32 SmokeSabotageCount = 0;
	double SmokeSabotageSince = 0.0;
	bool bSmokeMapShot = false;
	int32 SmokeRepairState = 0;
	double SmokeRepairSince = 0.0;
	double SmokeOutageSince = 0.0;
	bool bSmokeOutageShot = false;
	bool bSmokeAutoShift = false;
	float SmokeShiftRevert = 0.f;
	FString SmokeLastLooks;
	double SmokeShiftSince = 0.0;
	double SmokeDisguisedSince = 0.0;
	bool bSmokeShiftMenuShot = false;
	int32 SmokeLookShotState = 0;
	double SmokeLookShotSince = 0.0;
	TWeakObjectPtr<AHCCharacter> SmokeLookTarget;
	void ParseMeetingSmokeFlags();
	FString SmokeAutoVote;
	/** -HCAutoChat=<text>: write this line into the chat once per meeting ({name} = own name). */
	FString SmokeAutoChat;
	bool bSmokeChatSent = false;
	float SmokeAutoButton = 0.f;
	bool bSmokeMeetingParsed = false;
	int32 SmokeButtonState = 0;
	double SmokeButtonSince = 0.0;
	uint8 SmokeLastStage = 0;
	double SmokeStageSince = 0.0;
	/** The remembered look was sent to the server in this lobby. */
	bool bSavedLookSent = false;
	// --- Test bots of the jobs update (Core/HCPlayerControllerJobsSmoke.cpp) ---
	/** -HCAutoCrisis, -HCAutoAbility, -HCAutoEmote and pictures while carrying; true = busy, no tasks now. */
	bool TickAutoJobs(double Now, double InPhase);
	bool TickAutoCrisis(double Now);
	bool TickAutoAbility(double Now, double InPhase);
	void TickAutoEmote(double Now, double InPhase);
	/** -HCAutoWardrobe: in the lobby, to the wardrobe, change the look, pictures. */
	void TickAutoWardrobe(double Now);
	int32 SmokeCrisisState = 0;
	double SmokeCrisisSince = 0.0;
	double SmokeCrisisSeen = 0.0;
	bool bSmokeCrisisSent = false;
	int32 SmokeCrisisShots = 0;
	int32 SmokeAbilityState = 0;
	double SmokeAbilitySince = 0.0;
	bool bSmokeJobHudShot = false;
	bool bSmokeCarryShot = false;
	double SmokeEmoteNext = 0.0;
	int32 SmokeEmoteState = 0;
	int32 SmokeEmoteCount = 0;
	int32 SmokeWardrobeState = 0;
	double SmokeWardrobeSince = 0.0;
	void SendSavedLook();
	bool bSmokeStageShot = false;
	bool bSmokeVoteTried = false;
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
	/** -HCGhostCheck: after the ghost view, one jump to the next living player and a picture of it (0 = not yet, 1 = jumped, 2 = shot). */
	int32 SmokeGhostJumpState = 0;
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
	/** One picture of the interact prompt before the first press and one in the middle of a hold (-HCPhaseShots). */
	bool bSmokePromptShot = false;
	bool bSmokeHoldShot = false;
	double SmokePromptSeenAt = 0.0;
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
	int32 SmokeStoryShot = 0;
	bool bSmokeSkipVoted = false;

	int32 SmokeShotsLeft = 0;
	int32 SmokeShotIndex = 0;
	int32 SmokeWalkTicks = 0;
	bool bSmokeWalking = false;
	FString SmokeShotDir;
	FTimerHandle SmokeTimer;
};
