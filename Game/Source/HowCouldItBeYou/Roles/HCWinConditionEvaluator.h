#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"

/** Snapshot of the numbers that decide a match (docs/02 §5.5). */
struct HOWCOULDITBEYOU_API FHCWinInput
{
	int32 AliveCrew = 0;
	int32 AliveMonsters = 0;
	int32 CompletedTasks = 0;
	int32 TotalTasks = 0;
	/** The (last) monster was ejected in a meeting. */
	bool bMonsterEjected = false;
	/** The (last) monster disconnected. */
	bool bMonsterLeft = false;
	bool bTimerExpired = false;
	// From UHCMatchRules
	bool bCrewWinsByTasks = true;
	bool bTimerEnabled = false;
	int32 MinCrewAliveAtTimeout = 2;
	/** Off only for test rounds started with fewer than 3 players, where 1 vs 1 would end at once. */
	bool bParityEnabled = true;
};

struct HOWCOULDITBEYOU_API FHCWinConditionEvaluator
{
	/** Returns the winner (None = keep playing) and a reason text for the result screen. */
	static EHCWinner Evaluate(const FHCWinInput& In, FText& OutReason);
};
