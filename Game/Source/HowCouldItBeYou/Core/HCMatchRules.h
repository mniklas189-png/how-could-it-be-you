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
};
