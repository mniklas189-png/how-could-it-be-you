#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

/**
 * How one hand of the player figure holds the flashlight: the fingers closed around the handle and - while the light
 * is on - the arm raised so that the flashlight points where the player looks.
 * Everything is worked out once from the reference pose of the skeleton (HCFlashlightGrip::Build): the fingers bend
 * until they touch the handle, so the grip fits the hand and the flashlight without animations or models of its own.
 * Poses are arrays of component-space bone transforms, one per bone of the skeleton.
 */
struct FHCFlashlightGrip
{
	enum class EBone : uint8
	{
		Carried,   // keeps its place relative to the parent
		LowerArm,
		Hand,
		Twist,     // forearm bone that takes a part of the wrist's turn
		Finger,    // closed around the handle
	};

	/** A bone below the upper arm. */
	struct FBelow
	{
		int32 Bone = INDEX_NONE;
		int32 Parent = INDEX_NONE;
		EBone Kind = EBone::Carried;
		/** Twist: share of the wrist's turn (0..1). */
		float Share = 0.f;
		/** Finger: the bone seen from its parent, closed. */
		FTransform Closed = FTransform::Identity;
	};

	bool bValid = false;
	bool bLeft = false;
	int32 NumBones = 0;
	int32 UpperArm = INDEX_NONE;
	int32 LowerArm = INDEX_NONE;
	int32 Hand = INDEX_NONE;
	/** Every bone below the upper arm, parents before children. */
	TArray<FBelow> Below;
	/** Every bone that does not belong to the arm (the root excepted). */
	TArray<int32> Outside;
	/** Flashlight space (centre at the origin, lens towards +X, switch towards +Z) seen from the hand bone. */
	FTransform FlashlightInHand = FTransform::Identity;
	/** The reference pose in component space. */
	TArray<FTransform> RefPose;
};

namespace HCFlashlightGrip
{
	/** The part of the flashlight the hand closes around. */
	struct FHandle
	{
		/** Radius of the handle (cm). */
		float Radius = 2.f;
		/** The hand holds the flashlight this far behind its centre (cm). */
		float BehindCentre = 4.5f;
	};

	/** Not valid if the skeleton has no arm with fingers (names of the Epic mannequin: upperarm_r, hand_r, index_01_r ...). */
	HOWCOULDITBEYOU_API FHCFlashlightGrip Build(const FReferenceSkeleton& Skeleton, bool bLeft, const FHandle& Handle);

	/**
	 * Closes the fingers of the pose. With a flashlight target (component space) the arm moves so that the hand holds
	 * the flashlight exactly there; Raise blends from the arm as it is (0) to that (1). ElbowDirection: where the
	 * elbow points (component space).
	 */
	HOWCOULDITBEYOU_API void Apply(const FHCFlashlightGrip& Grip, TArray<FTransform>& Pose, const FTransform* Flashlight, const FVector& ElbowDirection, float Raise);

	/** Shrinks everything but the arm into the shoulder: what is left is an arm for the own view. */
	HOWCOULDITBEYOU_API void KeepOnlyArm(const FHCFlashlightGrip& Grip, TArray<FTransform>& Pose);

	/** Where the flashlight is in a pose (component space). */
	HOWCOULDITBEYOU_API FTransform FlashlightIn(const FHCFlashlightGrip& Grip, const TArray<FTransform>& Pose);
}
