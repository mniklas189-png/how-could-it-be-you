#pragma once

#include "CoreMinimal.h"
#include "Tasks/HCTaskTypes.h"

struct HOWCOULDITBEYOU_API FHCTaskAssignInput
{
	/** Only tasks whose stations all exist in the map. */
	TArray<FHCTaskDefinition> Available;
	int32 CommonTasks = 1;
	int32 LongTasks = 2;
	int32 ShortTasks = 3;
	/** The tasks of one player should touch at least this many rooms (docs/01 §5). */
	int32 MinAreas = 3;
	int32 MaxVisiblePerPlayer = 1;
};

/**
 * Pure task distribution (docs/02 §9.3). Every player gets the same common tasks plus own long and short ones,
 * no task twice, at most one visible task, spread over several rooms. The monster gets a list of the same
 * shape - whether it is "fake" is decided by the caller, not here. Deterministic for a seed.
 */
struct HOWCOULDITBEYOU_API FHCTaskAssigner
{
	static TMap<int32, TArray<FName>> Assign(const TArray<int32>& PlayerIds, const FHCTaskAssignInput& Input, int32 Seed, TArray<FString>* OutWarnings = nullptr);

	/** Number of distinct rooms the given tasks touch. */
	static int32 CountAreas(const TArray<FName>& TaskIds, const TArray<FHCTaskDefinition>& Definitions);
};
