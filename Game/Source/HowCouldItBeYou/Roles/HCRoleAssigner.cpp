#include "Roles/HCRoleAssigner.h"

#include "Math/RandomStream.h"

TMap<int32, EHCRole> FHCRoleAssigner::Assign(const TArray<int32>& PlayerIds, int32 NumMonsters, int32 Seed, const TMap<int32, EHCRole>& ForcedRoles)
{
	TMap<int32, EHCRole> Result;
	if (PlayerIds.IsEmpty())
	{
		return Result;
	}

	// At least one crew member must remain; with a single player that player is crew.
	const int32 WantedMonsters = FMath::Clamp(NumMonsters, 0, FMath::Max(0, PlayerIds.Num() - 1));

	int32 Monsters = 0;
	TArray<int32> Open;
	for (const int32 Id : PlayerIds)
	{
		if (const EHCRole* Forced = ForcedRoles.Find(Id))
		{
			Result.Add(Id, *Forced);
			Monsters += *Forced == EHCRole::Monster ? 1 : 0;
		}
		else
		{
			Open.Add(Id);
		}
	}

	// Fisher-Yates shuffle of the open players, then the first ones become monsters
	FRandomStream Random(Seed);
	for (int32 i = Open.Num() - 1; i > 0; --i)
	{
		Open.Swap(i, Random.RandRange(0, i));
	}
	for (const int32 Id : Open)
	{
		const bool bMonster = Monsters < WantedMonsters;
		Result.Add(Id, bMonster ? EHCRole::Monster : EHCRole::Crew);
		Monsters += bMonster ? 1 : 0;
	}
	return Result;
}
