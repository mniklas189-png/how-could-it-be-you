#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"

/** Pure role assignment (docs/02 §5.4). Deterministic for a given seed, so it can be unit-tested. */
struct HOWCOULDITBEYOU_API FHCRoleAssigner
{
	/**
	 * @param PlayerIds    all players of the round
	 * @param NumMonsters  wanted monsters (clamped so that at least one crew member remains)
	 * @param Seed         fixed in tests, random in the game
	 * @param ForcedRoles  from hc.ForceRole, take priority
	 */
	static TMap<int32, EHCRole> Assign(const TArray<int32>& PlayerIds, int32 NumMonsters, int32 Seed, const TMap<int32, EHCRole>& ForcedRoles);
};
