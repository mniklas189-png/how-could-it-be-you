#include "World/HCModelAssets.h"

#include "Core/HCLog.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

UHCModelSet* HCModels::Get()
{
	static TWeakObjectPtr<UHCModelSet> Cached;
	static bool bMissing = false;
	if (UHCModelSet* Set = Cached.Get())
	{
		return Set;
	}
	if (bMissing)
	{
		return nullptr;
	}
	const TCHAR* Package = TEXT("/Game/HowCouldItBeYou/Models/DA_Models");
	if (!FPackageName::DoesPackageExist(Package))
	{
		bMissing = true;
		UE_LOG(LogHC, Log, TEXT("No model set (%s): using the greybox figures"), Package);
		return nullptr;
	}
	UHCModelSet* Set = Cast<UHCModelSet>(FSoftObjectPath(TEXT("/Game/HowCouldItBeYou/Models/DA_Models.DA_Models")).TryLoad());
	Cached = Set;
	bMissing = Set == nullptr;
	if (Set)
	{
		UE_LOG(LogHC, Log, TEXT("Model set loaded: character %s, monster %s"), *GetNameSafe(Set->CharacterMesh), *GetNameSafe(Set->MonsterMesh));
	}
	return Set;
}
