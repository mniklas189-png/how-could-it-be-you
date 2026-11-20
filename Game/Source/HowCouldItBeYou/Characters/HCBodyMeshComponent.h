#pragma once

#include "CoreMinimal.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HCBodyMeshComponent.generated.h"

/**
 * Body of the player figure. Works like the engine's own, except that the finished pose of every frame can be
 * changed before it is shown: this is how the hand closes around the flashlight and the arm comes up
 * (AHCCharacter::EditBodyPose), on top of whatever the animation plays.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCBodyMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	/** Called with the pose of this frame (component space, one transform per bone) right before it becomes visible. */
	TFunction<void(TArray<FTransform>&)> PoseEdit;

	virtual void FinalizeBoneTransform() override;
};

/**
 * The arm in the own view: the figure's own body, of which only one arm is left (HCFlashlightGrip::KeepOnlyArm).
 * It plays no animation; the owner writes every pose itself.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCViewArmComponent : public UPoseableMeshComponent
{
	GENERATED_BODY()

public:
	UHCViewArmComponent(const FObjectInitializer& ObjectInitializer);

	/** Fill writes the whole pose (component space, one transform per bone); it is shown from this frame on. */
	void ShowPose(TFunctionRef<void(TArray<FTransform>&)> Fill);

	/** The pose only ever comes from ShowPose. */
	virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction = nullptr) override {}
};
