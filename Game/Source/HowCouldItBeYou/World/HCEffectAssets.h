#pragma once

#include "CoreMinimal.h"
#include "Misc/PackageName.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "HCEffectAssets.generated.h"

/** The game's own effect assets in /Game/HowCouldItBeYou/Effects, written by Tools/Setup/SetupEffects.py (always cooked). */
namespace HCEffects
{
	/** Null if the asset is missing; effects then fall back to plain shapes. */
	template <typename T>
	T* Load(const FString& Name)
	{
		const FString Package = TEXT("/Game/HowCouldItBeYou/Effects/") + Name;
		return FPackageName::DoesPackageExist(Package) ? Cast<T>(FSoftObjectPath(Package + TEXT(".") + Name).TryLoad()) : nullptr;
	}
}

/**
 * Loads blood and slime when a game world starts and keeps them while it lives, so the first kill or the first
 * shapeshift of a round does not stall on loading pictures (and the editor has them compiled by then).
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCEffectPreloader : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Loaded;
};
