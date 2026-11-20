#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

/** The emotes of the wheel (UI/HCEmoteWheel). Order = the slots of the wheel, clockwise from the top. */
enum class EHCEmote : uint8
{
	Wave,
	ComeHere,
	Point,
	Stop,
	Yes,
	No,
	Shrug,
	HandsUp,
	Count
};

/**
 * Gestures of the player figure without animations: the arms are moved to targets around the shoulders (two-bone
 * solve), the head nods or shakes, all on top of whatever the animation plays, through the same pose edit as the held
 * flashlight (UHCBodyMeshComponent, AHCCharacter::EditBodyPose). Targets are given in actor space (X forward,
 * Y right, Z up), so they do not depend on how the bones of the model are turned.
 */
struct FHCGestureRig
{
	bool bValid = false;
	TArray<int32> Parents;
	int32 Head = INDEX_NONE;
	/** 0 = right, 1 = left. */
	int32 UpperArm[2] = { INDEX_NONE, INDEX_NONE };
	int32 LowerArm[2] = { INDEX_NONE, INDEX_NONE };
	int32 Hand[2] = { INDEX_NONE, INDEX_NONE };
};

namespace HCGesture
{
	/** How long an emote plays (seconds), blend in and out included. */
	constexpr float Seconds = 2.6f;

	/** Not valid if the skeleton lacks the bones (names of the Epic mannequin: head, upperarm_r, lowerarm_r, hand_r ...). */
	HOWCOULDITBEYOU_API FHCGestureRig Build(const FReferenceSkeleton& Skeleton);

	/**
	 * Changes the pose (component space, one transform per bone) for an emote at Time seconds after it started.
	 * ActorToBody: actor space -> component space of the body. Nothing happens outside 0..Seconds.
	 */
	HOWCOULDITBEYOU_API void Apply(const FHCGestureRig& Rig, TArray<FTransform>& Pose, EHCEmote Emote, float Time, const FTransform& ActorToBody);

	/** Does the emote need the arms (not possible while carrying something with both hands)? */
	HOWCOULDITBEYOU_API bool UsesArms(EHCEmote Emote);

	/** Name on the wheel, "Winken" ... */
	HOWCOULDITBEYOU_API FText Name(EHCEmote Emote);
	/** Word above the figure while it plays, "HALLO!" ... */
	HOWCOULDITBEYOU_API FText Shout(EHCEmote Emote);
}
