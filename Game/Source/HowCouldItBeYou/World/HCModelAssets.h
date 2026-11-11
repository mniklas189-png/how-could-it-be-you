#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HCModelAssets.generated.h"

class UAnimInstance;
class UAnimSequence;
class USkeletalMesh;

/**
 * The real models of the game in one asset (/Game/HowCouldItBeYou/Models/DA_Models, written by
 * Tools/Setup/SetupModels.py). The models themselves come from store packs that are NOT part of the public
 * repository (their licences only allow shipping them inside the packaged game). Without the packs every entry
 * is empty and the game falls back to the greybox figures.
 */
UCLASS(BlueprintType)
class HOWCOULDITBEYOU_API UHCModelSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Body of every player. Looks along +Y, feet at the origin (Epic mannequin convention). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	TObjectPtr<USkeletalMesh> CharacterMesh;

	/** Locomotion from the character's own movement (idle, walk, run). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	TSubclassOf<UAnimInstance> CharacterAnimClass;

	/** Material slots that take the suit colour of the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	TArray<FName> TintSlots;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	FName TintParameter = TEXT("Tint");

	/** True form of the Wandler, shown to the victim in the jumpscare. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<USkeletalMesh> MonsterMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<UAnimSequence> MonsterAttack;
};

namespace HCModels
{
	/** The model set, or null if the asset does not exist. Loaded once. */
	HOWCOULDITBEYOU_API UHCModelSet* Get();
}
