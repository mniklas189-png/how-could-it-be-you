#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Tasks/HCTaskTypes.h"
#include "HCMatchRules.generated.h"

/**
 * Rules of one game mode, one ini section per mode: "[Infiltration HCMatchRules]" in DefaultGame.ini (docs/02 §17).
 * Missing values fall back to these C++ defaults (= Infiltration). M2 uses the role and win values; the
 * ability/meeting values follow with their milestones.
 */
UCLASS(Config = Game, PerObjectConfig)
class HOWCOULDITBEYOU_API UHCMatchRules : public UObject
{
	GENERATED_BODY()

public:
	/** Creates the rules object for a mode id and reads its ini section. */
	static UHCMatchRules* Load(UObject* Outer, FName ModeId);

	UPROPERTY(Config)
	FString DisplayName = TEXT("Infiltration");

	UPROPERTY(Config)
	int32 NumMonsters = 1;

	UPROPERTY(Config)
	bool bCrewWinsByTasks = true;

	UPROPERTY(Config)
	bool bTimerEnabled = false;

	UPROPERTY(Config)
	int32 MinCrewAliveAtTimeout = 2;

	UPROPERTY(Config)
	float MatchTimeLimit = 720.f;

	// --- Kill (docs/01 §12) ---
	UPROPERTY(Config)
	float KillCooldown = 30.f;

	/** First kill at the earliest this many seconds after the round started. */
	UPROPERTY(Config)
	float FirstKillDelay = 20.f;

	/** Monster and victim are locked for this long. */
	UPROPERTY(Config)
	float KillLockDuration = 2.5f;

	// --- Tasks per player (docs/01 §12) ---
	UPROPERTY(Config)
	int32 CommonTasks = 1;

	UPROPERTY(Config)
	int32 LongTasks = 2;

	UPROPERTY(Config)
	int32 ShortTasks = 3;

	UPROPERTY(Config)
	EHCTaskBarVisibility TaskBarVisibility = EHCTaskBarVisibility::Always;

	/** Used from M4 on: tasks done by ghosts count towards the bar. */
	UPROPERTY(Config)
	bool bGhostTasksCount = true;

	// --- Meeting (docs/01 §12, docs/02 §8) ---
	UPROPERTY(Config)
	float GatheringTime = 3.f;

	UPROPERTY(Config)
	float DiscussionTime = 60.f;

	UPROPERTY(Config)
	float VotingTime = 45.f;

	UPROPERTY(Config)
	float ResultsTime = 5.f;

	UPROPERTY(Config)
	float EjectionTime = 8.f;

	UPROPERTY(Config)
	int32 EmergencyMeetingsPerPlayer = 1;

	/** The emergency button is locked for this long after every meeting. */
	UPROPERTY(Config)
	float EmergencyButtonCooldown = 20.f;

	/** The ejection screen says whether it was the Wandler. */
	UPROPERTY(Config)
	bool bConfirmEjectRole = true;

	UPROPERTY(Config)
	bool bAnonymousVotes = false;

	// --- Shapeshift (docs/01 §12, docs/02 §6) ---
	/** How long a disguise lasts; 0 = until the Wandler ends it. */
	UPROPERTY(Config)
	float ShapeshiftDuration = 45.f;

	/** From the end of a disguise; also at round start and after every meeting. 0 = none. */
	UPROPERTY(Config)
	float ShapeshiftCooldown = 30.f;

	UPROPERTY(Config)
	float ShapeshiftTransformTime = 1.5f;

	// --- True form: the Wandler shows itself. Faster and tireless, but everybody sees the monster and its name. ---
	UPROPERTY(Config)
	float TrueFormDuration = 20.f;

	/** From the end of the true form; shared with the disguise (one transformation at a time). */
	UPROPERTY(Config)
	float TrueFormCooldown = 60.f;

	UPROPERTY(Config)
	float TrueFormSpeedFactor = 1.4f;

	/** Off: a "dead person walking" stays until the disguise runs out (docs/02 §6.6). */
	UPROPERTY(Config)
	bool bRevertWhenTargetDies = false;

	UPROPERTY(Config)
	bool bResetShapeshiftCooldownAfterMeeting = true;

	// --- Sabotage (docs/01 §7, docs/02 §11) ---
	/** No sabotage for this long after round start and after every meeting. */
	UPROPERTY(Config)
	float SabotageStartDelay = 30.f;

	/** The emergency power comes back by itself after this long. */
	UPROPERTY(Config)
	float PowerOutageMaxDuration = 90.f;

	/** From the end of an outage. */
	UPROPERTY(Config)
	float PowerSabotageCooldown = 45.f;

	UPROPERTY(Config)
	float DoorLockDuration = 10.f;

	/** From the end of a door lock. */
	UPROPERTY(Config)
	float DoorSabotageCooldown = 30.f;
};
