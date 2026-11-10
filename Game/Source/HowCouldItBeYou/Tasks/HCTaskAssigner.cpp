#include "Tasks/HCTaskAssigner.h"

#include "Math/RandomStream.h"

namespace
{
	const FHCTaskDefinition* FindDef(const TArray<FHCTaskDefinition>& Defs, FName Id)
	{
		return Defs.FindByPredicate([Id](const FHCTaskDefinition& D) { return D.TaskId == Id; });
	}

	void Shuffle(TArray<FName>& Array, FRandomStream& Random)
	{
		for (int32 i = Array.Num() - 1; i > 0; --i)
		{
			Array.Swap(i, Random.RandRange(0, i));
		}
	}
}

int32 FHCTaskAssigner::CountAreas(const TArray<FName>& TaskIds, const TArray<FHCTaskDefinition>& Definitions)
{
	TSet<FName> Rooms;
	for (const FName Id : TaskIds)
	{
		if (const FHCTaskDefinition* Def = FindDef(Definitions, Id))
		{
			Rooms.Append(Def->GetRooms());
		}
	}
	return Rooms.Num();
}

TMap<int32, TArray<FName>> FHCTaskAssigner::Assign(const TArray<int32>& PlayerIds, const FHCTaskAssignInput& Input, int32 Seed, TArray<FString>* OutWarnings)
{
	TMap<int32, TArray<FName>> Result;
	TArray<FName> CommonPool, LongPool, ShortPool;
	for (const FHCTaskDefinition& Def : Input.Available)
	{
		if (Def.Steps.IsEmpty())
		{
			continue;
		}
		TArray<FName>& Pool = Def.Category == EHCTaskCategory::Common ? CommonPool : Def.Category == EHCTaskCategory::Long ? LongPool : ShortPool;
		Pool.AddUnique(Def.TaskId);
	}

	auto Warn = [OutWarnings](const FString& Text)
	{
		if (OutWarnings)
		{
			OutWarnings->AddUnique(Text);
		}
	};
	if (CommonPool.Num() < Input.CommonTasks)
	{
		Warn(FString::Printf(TEXT("Only %d common task(s) available, %d wanted"), CommonPool.Num(), Input.CommonTasks));
	}
	if (LongPool.Num() < Input.LongTasks)
	{
		Warn(FString::Printf(TEXT("Only %d long task(s) available, %d wanted"), LongPool.Num(), Input.LongTasks));
	}
	if (ShortPool.Num() < Input.ShortTasks)
	{
		Warn(FString::Printf(TEXT("Only %d short task(s) available, %d wanted"), ShortPool.Num(), Input.ShortTasks));
	}

	// Common tasks: the same for everyone
	FRandomStream CommonRandom(Seed);
	Shuffle(CommonPool, CommonRandom);
	CommonPool.SetNum(FMath::Min(CommonPool.Num(), FMath::Max(0, Input.CommonTasks)));

	auto IsVisible = [&Input](FName Id)
	{
		const FHCTaskDefinition* Def = FindDef(Input.Available, Id);
		return Def && Def->bVisible;
	};
	const int32 FullCount = CommonPool.Num() + FMath::Min(FMath::Max(0, Input.LongTasks), LongPool.Num()) + FMath::Min(FMath::Max(0, Input.ShortTasks), ShortPool.Num());

	int32 PlayerIndex = 0;
	for (const int32 PlayerId : PlayerIds)
	{
		FRandomStream Random(Seed * 7919 + (++PlayerIndex) * 104729 + PlayerId);
		TArray<FName> Best;
		int32 BestAreas = -1;
		// A few attempts; keep the fullest list that touches the most rooms, stop once the rule is met
		for (int32 Attempt = 0; Attempt < 24; ++Attempt)
		{
			TArray<FName> Long = LongPool, Short = ShortPool;
			Shuffle(Long, Random);
			Shuffle(Short, Random);

			TArray<FName> List = CommonPool;
			int32 Visible = 0;
			for (const FName Id : List)
			{
				Visible += IsVisible(Id) ? 1 : 0;
			}
			auto Take = [&](const TArray<FName>& Pool, int32 Count)
			{
				int32 Taken = 0;
				for (const FName Id : Pool)
				{
					if (Taken >= Count)
					{
						break;
					}
					if (List.Contains(Id))
					{
						continue;
					}
					if (IsVisible(Id))
					{
						if (Visible >= Input.MaxVisiblePerPlayer)
						{
							continue;
						}
						++Visible;
					}
					List.Add(Id);
					++Taken;
				}
			};
			Take(Long, Input.LongTasks);
			Take(Short, Input.ShortTasks);

			const int32 Areas = CountAreas(List, Input.Available);
			if (List.Num() > Best.Num() || (List.Num() == Best.Num() && Areas > BestAreas))
			{
				Best = List;
				BestAreas = Areas;
			}
			if (BestAreas >= Input.MinAreas && Best.Num() >= FullCount)
			{
				break;
			}
		}
		if (BestAreas < Input.MinAreas && Best.Num() > 0)
		{
			Warn(FString::Printf(TEXT("Tasks of a player only touch %d room(s), %d wanted"), BestAreas, Input.MinAreas));
		}
		Result.Add(PlayerId, Best);
	}
	return Result;
}
