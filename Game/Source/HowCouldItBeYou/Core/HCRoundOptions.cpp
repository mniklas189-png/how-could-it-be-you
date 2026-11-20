#include "Core/HCRoundOptions.h"

#include "Core/HCGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Roles/HCJobs.h"

namespace HCRoundOptions
{
	const TCHAR* const VentsKey = TEXT("VentsEnabled");
	const TCHAR* const SecondMonsterKey = TEXT("SecondMonster");
	const TCHAR* const JobsKey = TEXT("JobsEnabled");
	const TCHAR* const JobMaskKey = TEXT("JobMask");

	namespace
	{
		int32 Read(const UObject* WorldContext, const TCHAR* Key, int32 Default, const TCHAR* DevFlag)
		{
			const UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
			const UHCGameInstance* GI = World ? Cast<UHCGameInstance>(World->GetGameInstance()) : nullptr;
			int32 Value = GI ? FMath::RoundToInt(GI->GetOption(Key, static_cast<float>(Default))) : Default;
#if !UE_BUILD_SHIPPING
			FParse::Value(FCommandLine::Get(), DevFlag, Value);
#endif
			return Value;
		}
	}

	bool AreVentsEnabled(const UObject* WorldContext)
	{
		return Read(WorldContext, VentsKey, 1, TEXT("HCVents=")) != 0;
	}

	bool IsSecondMonsterEnabled(const UObject* WorldContext)
	{
		return Read(WorldContext, SecondMonsterKey, 1, TEXT("HCSecondMonster=")) != 0;
	}

	bool AreJobsEnabled(const UObject* WorldContext)
	{
		return Read(WorldContext, JobsKey, 1, TEXT("HCJobs=")) != 0;
	}

	uint8 GetJobMask(const UObject* WorldContext)
	{
		return static_cast<uint8>(Read(WorldContext, JobMaskKey, HCJobs::AllMask, TEXT("HCJobMask=")) & HCJobs::AllMask);
	}

	int32 MonstersFor(int32 BaseMonsters, int32 Players, bool bSecondMonster)
	{
		int32 Monsters = FMath::Max(1, BaseMonsters) + (bSecondMonster && Players >= SecondMonsterPlayers ? 1 : 0);
#if !UE_BUILD_SHIPPING
		FParse::Value(FCommandLine::Get(), TEXT("HCMonsters="), Monsters);
#endif
		return Monsters;
	}
}
