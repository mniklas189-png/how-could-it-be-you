#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HCModelAssets.generated.h"

class UAnimInstance;
class UAnimSequence;
class USkeletalMesh;
class AActor;
class USoundBase;
class UStaticMesh;

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

	/** Flashlight every player holds: in the lower right of the own view, in the right hand for the others. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	TObjectPtr<UStaticMesh> FlashlightMesh;

	/** Turn that brings the lens of FlashlightMesh to +X. The model is centred and scaled by the game. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	FRotator FlashlightRotation = FRotator::ZeroRotator;

	/** Material slot of the lens; it glows while the flashlight is on. None = no glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Player")
	FName FlashlightLensSlot;

	/** True form of the Wandler, shown to the victim in the jumpscare. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<USkeletalMesh> MonsterMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<UAnimSequence> MonsterAttack;

	/** The Wandler walking around in its true form (the monster has no animation blueprint). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<UAnimSequence> MonsterIdle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<UAnimSequence> MonsterWalk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Monster")
	TObjectPtr<UAnimSequence> MonsterRun;

	// --- Station: building pieces that are laid over the generated layout (AHCStationBuilder::BuildDressing).
	//     The expected shapes are documented there. Without floor and wall the station stays greybox.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Station")
	TObjectPtr<UStaticMesh> FloorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Station")
	TObjectPtr<UStaticMesh> CeilingMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Station")
	TObjectPtr<UStaticMesh> WallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Station")
	TObjectPtr<UStaticMesh> WallTrimMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Station")
	TObjectPtr<UStaticMesh> CeilingLightMesh;

	// --- Objects: real models that stand in for the grey blocks (keys Task0..TaskN, Charger, FuseBox, ButtonBase,
	//     DoorLeaf, DoorGlass) and things that just stand around (the keys used by AHCStationBuilder::BuildProps).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Objects")
	TMap<FName, TObjectPtr<UStaticMesh>> Props;

	/** Turn (yaw, degrees) that brings the front of a prop to +X. Missing = 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Objects")
	TMap<FName, float> PropYaws;

	UStaticMesh* FindProp(FName Key, float* OutYaw = nullptr) const;

	/** Real recordings that replace the built-in sounds, by the name of the sound (Tools\SetupSounds.bat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HC|Sounds")
	TMap<FName, TObjectPtr<USoundBase>> Sounds;
};

namespace HCModels
{
	/** The model set, or null if the asset does not exist. Loaded once. */
	HOWCOULDITBEYOU_API UHCModelSet* Get();

	/** Component tag of every stand-in made by Dress (the interaction outline looks for it). */
	extern HOWCOULDITBEYOU_API const FName DressTag;

	/**
	 * Visual-only stand-in for a grey block: the prop is turned to the front, scaled evenly until it fills Box
	 * (actor space, cm), stood on the bottom of the box and pushed against its back (-X) or centred.
	 * Returns the space it really takes; not valid without the model. The block itself stays as collision.
	 * OutPivot (optional) gets where the model's own origin ended up (actor space, cm); for a model with parts
	 * sticking out on one side that is a better centre than the middle of the returned box.
	 */
	HOWCOULDITBEYOU_API FBox Dress(AActor* Owner, FName Key, const FBox& Box, bool bAgainstBack = true, FVector* OutPivot = nullptr);
}
