#include "World/HCEffectAssets.h"

#include "Engine/World.h"

bool UHCEffectPreloader::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UHCEffectPreloader::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	TArray<FString> Names = { TEXT("M_BloodDecal"), TEXT("M_SlimeDecal") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Names.Add(FString::Printf(TEXT("T_Blood_%d"), Index));
		Names.Add(FString::Printf(TEXT("T_Slime_%d"), Index));
		Names.Add(FString::Printf(TEXT("T_Slime_%d_N"), Index));
	}
	for (const FString& Name : Names)
	{
		if (UObject* Asset = HCEffects::Load<UObject>(Name))
		{
			Loaded.Add(Asset);
		}
	}
}
