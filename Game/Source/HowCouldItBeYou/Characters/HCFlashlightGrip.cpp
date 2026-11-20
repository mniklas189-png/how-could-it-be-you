#include "Characters/HCFlashlightGrip.h"
#include "ReferenceSkeleton.h"

namespace HCFlashlightGripDetail
{
	/**
	 * The flashlight is held the way a big torch is carried: across the palm, the lens coming out on the side of
	 * the little finger. Raised in front of the shoulder it then points ahead with a straight wrist.
	 * The handle is turned this far (degrees) from straight across the palm, its lens end towards the fingers.
	 */
	constexpr float HandleTilt = 30.f;
	/** Where the handle crosses the palm, as a share of the way from the wrist to the knuckles. */
	constexpr float AlongPalm = 0.7f;
	/** The thumb leans this much towards its own side of the fist, so that it ends next to the index finger, not in it. */
	constexpr float ThumbLean = 0.25f;
	/** From the bones of the hand to the skin of the palm (cm). */
	constexpr float PalmDepth = 1.5f;
	/** Half the thickness of a finger and of the thumb (cm). */
	constexpr float FingerHalf = 0.8f;
	constexpr float ThumbHalf = 0.9f;
	/** The fingertip lies this far beyond the last finger bone, in lengths of the bone before it. */
	constexpr float TipLength = 0.85f;

	struct FHandleShape
	{
		FVector Centre = FVector::ZeroVector;
		FVector Axis = FVector::ForwardVector;
		float Radius = 2.f;

		/** Distance of a piece of bone from the middle line of the handle. */
		float Distance(const FVector& From, const FVector& To) const
		{
			FVector OnBone, OnAxis;
			FMath::SegmentDistToSegmentSafe(From, To, Centre - Axis * 40.f, Centre + Axis * 40.f, OnBone, OnAxis);
			return static_cast<float>(FVector::Dist(OnBone, OnAxis));
		}
	};

	/** The bones of one finger in component space; At has one entry more, the fingertip. */
	struct FFinger
	{
		TArray<FVector, TInlineAllocator<4>> At;
		TArray<FQuat, TInlineAllocator<3>> Turn;
	};

	/**
	 * Turns one joint of a finger (and everything after it) around Axis, from FromDeg towards ToDeg, and stops where
	 * the bone touches the handle. A bone that never reaches the handle goes as close as it gets.
	 */
	static void CloseJoint(FFinger& Finger, int32 Joint, const FVector& Axis, float FromDeg, float ToDeg, const FHandleShape& Handle)
	{
		const FVector Pivot = Finger.At[Joint];
		const FVector Bone = Finger.At[Joint + 1] - Pivot;
		const float Step = ToDeg >= FromDeg ? 1.f : -1.f;
		float Chosen = FromDeg;
		float Nearest = TNumericLimits<float>::Max();
		float NearestAt = FromDeg;
		bool bTouched = false;
		for (float Deg = FromDeg; Step > 0.f ? Deg <= ToDeg : Deg >= ToDeg; Deg += Step)
		{
			const FVector Turned = FQuat(Axis, FMath::DegreesToRadians(Deg)).RotateVector(Bone);
			// The joint itself may already lie on the handle (the bone before it ended there): the far part counts
			const float Distance = Handle.Distance(Pivot + Turned * 0.3f, Pivot + Turned);
			if (Distance <= Handle.Radius)
			{
				bTouched = true;
				break;
			}
			Chosen = Deg;
			if (Distance < Nearest)
			{
				Nearest = Distance;
				NearestAt = Deg;
			}
		}
		if (!bTouched)
		{
			Chosen = NearestAt;
		}
		const FQuat Turn(Axis, FMath::DegreesToRadians(Chosen));
		for (int32 Index = Joint + 1; Index < Finger.At.Num(); ++Index)
		{
			Finger.At[Index] = Pivot + Turn.RotateVector(Finger.At[Index] - Pivot);
		}
		for (int32 Index = Joint; Index < Finger.Turn.Num(); ++Index)
		{
			Finger.Turn[Index] = Turn * Finger.Turn[Index];
		}
	}

	/** Signed angle (radians) that turns From into To around Axis; both are flattened onto the plane across Axis first. */
	static float AngleAround(const FVector& Axis, const FVector& From, const FVector& To)
	{
		const FVector A = From - Axis * (From | Axis);
		const FVector B = To - Axis * (To | Axis);
		if (A.SizeSquared() < 0.0025 || B.SizeSquared() < 0.0025)
		{
			return 0.f; // one of them runs along the axis: no turn to speak of
		}
		return static_cast<float>(FMath::Atan2((A ^ B) | Axis, A | B));
	}
}

FHCFlashlightGrip HCFlashlightGrip::Build(const FReferenceSkeleton& Skeleton, bool bLeft, const FHandle& Handle)
{
	using namespace HCFlashlightGripDetail;
	FHCFlashlightGrip Grip;
	Grip.bLeft = bLeft;
	const FString Side = bLeft ? TEXT("_l") : TEXT("_r");
	auto Find = [&Skeleton, &Side](const TCHAR* Name) { return Skeleton.FindBoneIndex(FName(*(FString(Name) + Side))); };
	Grip.UpperArm = Find(TEXT("upperarm"));
	Grip.LowerArm = Find(TEXT("lowerarm"));
	Grip.Hand = Find(TEXT("hand"));
	// Thumb first, then index to little finger
	static const TCHAR* const FingerNames[5] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };
	int32 FingerBones[5][3];
	bool bAllThere = Grip.UpperArm != INDEX_NONE && Grip.LowerArm != INDEX_NONE && Grip.Hand != INDEX_NONE;
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			FingerBones[Finger][Joint] = Find(*FString::Printf(TEXT("%s_%02d"), FingerNames[Finger], Joint + 1));
			bAllThere &= FingerBones[Finger][Joint] != INDEX_NONE;
		}
	}
	if (!bAllThere)
	{
		return Grip;
	}

	const int32 Num = Skeleton.GetNum();
	Grip.NumBones = Num;
	const TArray<FTransform>& RefLocal = Skeleton.GetRefBonePose();
	TArray<FTransform>& Ref = Grip.RefPose;
	Ref.SetNum(Num);
	TArray<bool> bInArm;
	bInArm.Init(false, Num);
	bInArm[Grip.UpperArm] = true;
	for (int32 Bone = 0; Bone < Num; ++Bone)
	{
		const int32 Parent = Skeleton.GetParentIndex(Bone);
		Ref[Bone] = Parent == INDEX_NONE ? RefLocal[Bone] : RefLocal[Bone] * Ref[Parent];
		if (Parent != INDEX_NONE && bInArm[Parent])
		{
			bInArm[Bone] = true;
			FHCFlashlightGrip::FBelow& Below = Grip.Below.AddDefaulted_GetRef();
			Below.Bone = Bone;
			Below.Parent = Parent;
		}
		else if (Bone != 0 && Bone != Grip.UpperArm)
		{
			Grip.Outside.Add(Bone);
		}
	}
	if (!bInArm[Grip.LowerArm] || !bInArm[Grip.Hand])
	{
		return Grip;
	}

	// The hand in the reference pose: along the palm, across it, and out of it
	const FVector Wrist = Ref[Grip.Hand].GetLocation();
	const FVector IndexKnuckle = Ref[FingerBones[1][0]].GetLocation();
	const FVector LittleKnuckle = Ref[FingerBones[4][0]].GetLocation();
	const FVector Knuckles = (IndexKnuckle + Ref[FingerBones[2][0]].GetLocation() + Ref[FingerBones[3][0]].GetLocation() + LittleKnuckle) * 0.25;
	const FVector Along = (Knuckles - Wrist).GetSafeNormal();
	const FVector Across = ((IndexKnuckle - LittleKnuckle) - Along * ((IndexKnuckle - LittleKnuckle) | Along)).GetSafeNormal();
	const FVector Palm = (bLeft ? (Across ^ Along) : (Along ^ Across)).GetSafeNormal();
	if (Along.IsNearlyZero() || Across.IsNearlyZero())
	{
		return Grip;
	}

	// The handle lies across the palm just behind the knuckles, its lens end on the side of the little finger
	const float Tilt = FMath::DegreesToRadians(HandleTilt);
	FHandleShape Shape;
	Shape.Axis = (Along * FMath::Sin(Tilt) - Across * FMath::Cos(Tilt)).GetSafeNormal();
	Shape.Centre = Wrist + Along * (FVector::Dist(Knuckles, Wrist) * AlongPalm) + Palm * (Handle.Radius + PalmDepth);

	// The top of the flashlight (its switch) is on the side of the fingers: held up, the forearm comes from below
	const FVector Up = (Along - Shape.Axis * (Along | Shape.Axis)).GetSafeNormal();
	const FTransform FlashlightRef(FRotationMatrix::MakeFromXZ(Shape.Axis, Up).ToQuat(), Shape.Centre + Shape.Axis * Handle.BehindCentre);
	Grip.FlashlightInHand = FlashlightRef.GetRelativeTransform(Ref[Grip.Hand]);

	// The fingers close around the handle, joint by joint, each one until it touches
	TMap<int32, FTransform> Closed;
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		const bool bThumb = Finger == 0;
		FFinger Chain;
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			Chain.At.Add(Ref[FingerBones[Finger][Joint]].GetLocation());
			Chain.Turn.Add(Ref[FingerBones[Finger][Joint]].GetRotation());
		}
		Chain.At.Add(Chain.At[2] + (Chain.At[2] - Chain.At[1]) * TipLength);
		Shape.Radius = Handle.Radius + (bThumb ? ThumbHalf : FingerHalf);
		if (bThumb)
		{
			// The thumb comes from the wrist side and goes around the handle the other way. First the whole thumb
			// swings until it stands across the handle (leaning a little to its own side of the fist) ...
			const FVector Thumb = (Chain.At[3] - Chain.At[0]).GetSafeNormal();
			const FVector OwnSide = (Shape.Axis | Across) >= 0. ? Shape.Axis : -Shape.Axis;
			const FVector Wanted = ((Thumb - Shape.Axis * (Thumb | Shape.Axis)).GetSafeNormal() + OwnSide * ThumbLean).GetSafeNormal();
			const FQuat Swing = FQuat::FindBetweenNormals(Thumb, Wanted);
			for (int32 Index = 1; Index < Chain.At.Num(); ++Index)
			{
				Chain.At[Index] = Chain.At[0] + Swing.RotateVector(Chain.At[Index] - Chain.At[0]);
			}
			for (FQuat& Turn : Chain.Turn)
			{
				Turn = Swing * Turn;
			}
			// ... then it lifts off the palm and lies down over the handle, joint by joint
			const FVector Around = ((Palm ^ Along) | Shape.Axis) >= 0. ? Shape.Axis : -Shape.Axis;
			CloseJoint(Chain, 0, Around, -70.f, 50.f, Shape);
			CloseJoint(Chain, 1, Around, -20.f, 80.f, Shape);
			CloseJoint(Chain, 2, Around, -10.f, 80.f, Shape);
		}
		else
		{
			// Hinge of the finger: turning around it bends the finger towards the palm. The first joint starts
			// stretched, because a finger of the reference pose may already be bent into the handle.
			const FVector Hinge = ((Chain.At[1] - Chain.At[0]).GetSafeNormal() ^ Palm).GetSafeNormal();
			CloseJoint(Chain, 0, Hinge, -25.f, 95.f, Shape);
			CloseJoint(Chain, 1, Hinge, 0.f, 115.f, Shape);
			CloseJoint(Chain, 2, Hinge, 0.f, 80.f, Shape);
		}
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			const int32 Bone = FingerBones[Finger][Joint];
			const FTransform Now(Chain.Turn[Joint], Chain.At[Joint], Ref[Bone].GetScale3D());
			const FTransform Before = Joint == 0 ? Ref[Skeleton.GetParentIndex(Bone)] : FTransform(Chain.Turn[Joint - 1], Chain.At[Joint - 1], Ref[FingerBones[Finger][Joint - 1]].GetScale3D());
			Closed.Add(Bone, Now.GetRelativeTransform(Before));
		}
	}

	const FVector Elbow = Ref[Grip.LowerArm].GetLocation();
	const FVector Shoulder = Ref[Grip.UpperArm].GetLocation();
	for (FHCFlashlightGrip::FBelow& Below : Grip.Below)
	{
		const FString Name = Skeleton.GetBoneName(Below.Bone).ToString();
		if (Below.Bone == Grip.LowerArm)
		{
			Below.Kind = FHCFlashlightGrip::EBone::LowerArm;
		}
		else if (Below.Bone == Grip.Hand)
		{
			Below.Kind = FHCFlashlightGrip::EBone::Hand;
		}
		else if (const FTransform* Found = Closed.Find(Below.Bone))
		{
			Below.Kind = FHCFlashlightGrip::EBone::Finger;
			Below.Closed = *Found;
		}
		else if (Name.Contains(TEXT("twist")) && Below.Parent == Grip.LowerArm)
		{
			// The closer to the wrist, the more of the wrist's turn the skin of the forearm follows
			Below.Kind = FHCFlashlightGrip::EBone::Twist;
			Below.Share = FMath::Clamp(1.4f * static_cast<float>(FVector::Dist(Ref[Below.Bone].GetLocation(), Elbow) / FMath::Max(1., FVector::Dist(Wrist, Elbow))), 0.f, 1.f);
		}
		else if (Name.Contains(TEXT("twist")) && Below.Parent == Grip.UpperArm)
		{
			// The closer to the shoulder, the less the skin of the upper arm turns with it (negative: turned back)
			Below.Kind = FHCFlashlightGrip::EBone::Twist;
			Below.Share = -FMath::Clamp(1.f - static_cast<float>(FVector::Dist(Ref[Below.Bone].GetLocation(), Shoulder) / FMath::Max(1., FVector::Dist(Elbow, Shoulder))), 0.f, 1.f);
		}
	}
	Grip.bValid = true;
	return Grip;
}

void HCFlashlightGrip::Apply(const FHCFlashlightGrip& Grip, TArray<FTransform>& Pose, const FTransform* Flashlight, const FVector& ElbowDirection, float Raise)
{
	using namespace HCFlashlightGripDetail;
	if (!Grip.bValid || Pose.Num() != Grip.NumBones)
	{
		return;
	}
	// How every bone of the arm sits on its parent: kept, unless it is set below
	TArray<FTransform, TInlineAllocator<96>> Local;
	Local.Reserve(Grip.Below.Num());
	for (const FHCFlashlightGrip::FBelow& Below : Grip.Below)
	{
		Local.Add(Pose[Below.Bone].GetRelativeTransform(Pose[Below.Parent]));
	}

	const FQuat OldUpper = Pose[Grip.UpperArm].GetRotation();
	const FQuat OldLower = Pose[Grip.LowerArm].GetRotation();
	const FQuat OldHand = Pose[Grip.Hand].GetRotation();
	const FVector Shoulder = Pose[Grip.UpperArm].GetLocation();
	const FVector OldElbow = Pose[Grip.LowerArm].GetLocation();
	const FVector OldWrist = Pose[Grip.Hand].GetLocation();
	const FVector OldUpperDir = (OldElbow - Shoulder).GetSafeNormal();
	const FVector OldLowerDir = (OldWrist - OldElbow).GetSafeNormal();
	FQuat Upper = OldUpper, Lower = OldLower, Hand = OldHand;
	float UpperTwist = 0.f, WristTwist = 0.f;

	Raise = FMath::Clamp(Raise, 0.f, 1.f);
	if (Flashlight && Raise > 0.001f)
	{
		const FTransform HandTarget = Grip.FlashlightInHand.Inverse() * (*Flashlight);
		const double UpperLength = FVector::Dist(Shoulder, OldElbow);
		const double LowerLength = FVector::Dist(OldElbow, OldWrist);
		const FVector ToTarget = HandTarget.GetLocation() - Shoulder;
		const FVector Reach = ToTarget.GetSafeNormal(UE_SMALL_NUMBER, OldUpperDir);
		// Never quite stretched, never folded flat
		const double Distance = FMath::Clamp(ToTarget.Size(), FMath::Abs(UpperLength - LowerLength) + 1., (UpperLength + LowerLength) * 0.985);
		FVector Bend = ElbowDirection - Reach * (ElbowDirection | Reach);
		if (!Bend.Normalize())
		{
			Bend = (OldElbow - Shoulder) - Reach * ((OldElbow - Shoulder) | Reach);
			if (!Bend.Normalize())
			{
				FVector Other;
				Reach.FindBestAxisVectors(Bend, Other);
			}
		}
		const double CosShoulder = FMath::Clamp((UpperLength * UpperLength + Distance * Distance - LowerLength * LowerLength) / (2. * UpperLength * Distance), -1., 1.);
		const FVector Elbow = Shoulder + (Reach * CosShoulder + Bend * FMath::Sqrt(1. - CosShoulder * CosShoulder)) * UpperLength;
		const FVector Wrist = Shoulder + Reach * Distance;
		const FVector UpperDir = (Elbow - Shoulder).GetSafeNormal();
		const FVector LowerDir = (Wrist - Elbow).GetSafeNormal();

		// Shoulder: swing the upper arm over, then turn it around itself until the elbow bends in the right plane ...
		const FQuat Swing = FQuat::FindBetweenNormals(OldUpperDir, UpperDir);
		UpperTwist = AngleAround(UpperDir, Swing.RotateVector(OldLowerDir), LowerDir);
		const FQuat UpperTurn = FQuat(UpperDir, UpperTwist) * Swing;
		// ... so that the elbow only has to bend
		const FQuat LowerTurn = FQuat::FindBetweenNormals(UpperTurn.RotateVector(OldLowerDir), LowerDir) * UpperTurn;
		// How far the wrist is turned around the forearm compared to a hand that just came along
		const FQuat WristTurn = HandTarget.GetRotation() * (LowerTurn * OldHand).Inverse();
		WristTwist = FMath::Clamp(static_cast<float>(WristTurn.GetTwistAngle(LowerDir)), -2.2f, 2.2f) * Raise;
		UpperTwist *= Raise;

		Upper = FQuat::Slerp(OldUpper, UpperTurn * OldUpper, Raise);
		Lower = FQuat::Slerp(OldLower, LowerTurn * OldLower, Raise);
		Hand = FQuat::Slerp(OldHand, HandTarget.GetRotation(), Raise);
	}
	const FVector UpperAxis = (Upper * OldUpper.Inverse()).RotateVector(OldUpperDir);
	const FVector LowerAxis = (Lower * OldLower.Inverse()).RotateVector(OldLowerDir);

	Pose[Grip.UpperArm].SetRotation(Upper);
	for (int32 Index = 0; Index < Grip.Below.Num(); ++Index)
	{
		const FHCFlashlightGrip::FBelow& Below = Grip.Below[Index];
		FTransform Bone = (Below.Kind == FHCFlashlightGrip::EBone::Finger ? Below.Closed : Local[Index]) * Pose[Below.Parent];
		switch (Below.Kind)
		{
		case FHCFlashlightGrip::EBone::LowerArm:
			Bone.SetRotation(Lower);
			break;
		case FHCFlashlightGrip::EBone::Hand:
			Bone.SetRotation(Hand);
			break;
		case FHCFlashlightGrip::EBone::Twist:
			if (Below.Share >= 0.f)
			{
				Bone.SetRotation(FQuat(LowerAxis, WristTwist * Below.Share) * Bone.GetRotation());
			}
			else
			{
				Bone.SetRotation(FQuat(UpperAxis, UpperTwist * Below.Share) * Bone.GetRotation());
			}
			break;
		default:
			break;
		}
		Bone.NormalizeRotation();
		Pose[Below.Bone] = Bone;
	}
}

void HCFlashlightGrip::KeepOnlyArm(const FHCFlashlightGrip& Grip, TArray<FTransform>& Pose)
{
	if (!Grip.bValid || Pose.Num() != Grip.NumBones)
	{
		return;
	}
	const FTransform Gone(FQuat::Identity, Pose[Grip.UpperArm].GetLocation(), FVector::ZeroVector);
	for (const int32 Bone : Grip.Outside)
	{
		Pose[Bone] = Gone;
	}
}

FTransform HCFlashlightGrip::FlashlightIn(const FHCFlashlightGrip& Grip, const TArray<FTransform>& Pose)
{
	return Grip.bValid && Pose.IsValidIndex(Grip.Hand) ? Grip.FlashlightInHand * Pose[Grip.Hand] : FTransform::Identity;
}
