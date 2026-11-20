#include "Characters/HCGesture.h"

#include "ReferenceSkeleton.h"

namespace HCGestureDetail
{
	/** Signed angle (radians) from From to To around Axis, both seen in the plane across the axis. */
	static float AngleAround(const FVector& Axis, const FVector& From, const FVector& To)
	{
		const FVector A = (From - Axis * (From | Axis)).GetSafeNormal();
		const FVector B = (To - Axis * (To | Axis)).GetSafeNormal();
		if (A.IsNearlyZero() || B.IsNearlyZero())
		{
			return 0.f;
		}
		return FMath::Atan2(static_cast<float>((A ^ B) | Axis), static_cast<float>(A | B));
	}

	/** Eased weight: in over the first and out over the last 0.3 s. */
	static float Weight(float Time)
	{
		const float In = FMath::Clamp(Time / 0.3f, 0.f, 1.f);
		const float Out = FMath::Clamp((HCGesture::Seconds - Time) / 0.3f, 0.f, 1.f);
		const float W = FMath::Min(In, Out);
		return W * W * (3.f - 2.f * W);
	}

	/** What one arm does: wrist target relative to the shoulder (actor space, for the right arm; mirrored for the left). */
	struct FArmGoal
	{
		bool bActive = false;
		FVector Wrist = FVector::ZeroVector;
		/** Where the elbow points (actor space, right arm). */
		FVector Elbow = FVector(0.f, 0.6f, -1.f);
		/** Turn of the hand around the forearm (degrees; positive turns the palm of the right hand forward). */
		float Twist = 0.f;
		/** Bend of the wrist around the actor's right axis (degrees; positive lifts the fingers). */
		float Bend = 0.f;
	};

	/** Moves one arm so the wrist reaches the goal; Local holds the pose as parent-relative transforms. */
	static void SolveArm(const FHCGestureRig& Rig, int32 Side, const TArray<FTransform>& Pose, TArray<FTransform>& Local, const FArmGoal& Goal,
		const FTransform& ActorToBody, float W)
	{
		const int32 U = Rig.UpperArm[Side], L = Rig.LowerArm[Side], H = Rig.Hand[Side];
		const float Mirror = Side == 0 ? 1.f : -1.f;
		const FVector Shoulder = Pose[U].GetLocation();
		const FVector OldElbow = Pose[L].GetLocation();
		const FVector OldWrist = Pose[H].GetLocation();
		const FVector OldUpperDir = (OldElbow - Shoulder).GetSafeNormal();
		const FVector OldLowerDir = (OldWrist - OldElbow).GetSafeNormal();
		const double UpperLength = FVector::Dist(Shoulder, OldElbow);
		const double LowerLength = FVector::Dist(OldElbow, OldWrist);

		const FVector ShoulderActor = ActorToBody.InverseTransformPosition(Shoulder);
		const FVector WristActor = ShoulderActor + FVector(Goal.Wrist.X, Goal.Wrist.Y * Mirror, Goal.Wrist.Z);
		const FVector Target = ActorToBody.TransformPosition(WristActor);
		const FVector Hint = ActorToBody.TransformVectorNoScale(FVector(Goal.Elbow.X, Goal.Elbow.Y * Mirror, Goal.Elbow.Z).GetSafeNormal());

		const FVector ToTarget = Target - Shoulder;
		const FVector Reach = ToTarget.GetSafeNormal(UE_SMALL_NUMBER, OldUpperDir);
		const double Distance = FMath::Clamp(ToTarget.Size(), FMath::Abs(UpperLength - LowerLength) + 1., (UpperLength + LowerLength) * 0.985);
		FVector Bend = Hint - Reach * (Hint | Reach);
		if (!Bend.Normalize())
		{
			FVector Other;
			Reach.FindBestAxisVectors(Bend, Other);
		}
		const double CosShoulder = FMath::Clamp((UpperLength * UpperLength + Distance * Distance - LowerLength * LowerLength) / (2. * UpperLength * Distance), -1., 1.);
		const FVector Elbow = Shoulder + (Reach * CosShoulder + Bend * FMath::Sqrt(1. - CosShoulder * CosShoulder)) * UpperLength;
		const FVector Wrist = Shoulder + Reach * Distance;
		const FVector UpperDir = (Elbow - Shoulder).GetSafeNormal();
		const FVector LowerDir = (Wrist - Elbow).GetSafeNormal();

		const FQuat Swing = FQuat::FindBetweenNormals(OldUpperDir, UpperDir);
		const float UpperTwist = AngleAround(UpperDir, Swing.RotateVector(OldLowerDir), LowerDir);
		const FQuat UpperTurn = FQuat(UpperDir, UpperTwist) * Swing;
		const FQuat LowerTurn = FQuat::FindBetweenNormals(UpperTurn.RotateVector(OldLowerDir), LowerDir) * UpperTurn;
		const FVector Right = ActorToBody.TransformVectorNoScale(FVector::RightVector);
		const FQuat HandTurn = FQuat(Right * Mirror, FMath::DegreesToRadians(-Goal.Bend) * Mirror) * FQuat(LowerDir, FMath::DegreesToRadians(Goal.Twist) * Mirror) * LowerTurn;

		const FQuat Upper = FQuat::Slerp(Pose[U].GetRotation(), UpperTurn * Pose[U].GetRotation(), W);
		const FQuat Lower = FQuat::Slerp(Pose[L].GetRotation(), LowerTurn * Pose[L].GetRotation(), W);
		const FQuat Hand = FQuat::Slerp(Pose[H].GetRotation(), HandTurn * Pose[H].GetRotation(), W);
		const int32 Parent = Rig.Parents[U];
		const FQuat ParentRotation = Parent != INDEX_NONE ? Pose[Parent].GetRotation() : FQuat::Identity;
		Local[U].SetRotation((ParentRotation.Inverse() * Upper).GetNormalized());
		Local[L].SetRotation((Upper.Inverse() * Lower).GetNormalized());
		Local[H].SetRotation((Lower.Inverse() * Hand).GetNormalized());
	}
}

FHCGestureRig HCGesture::Build(const FReferenceSkeleton& Skeleton)
{
	FHCGestureRig Rig;
	const int32 Num = Skeleton.GetNum();
	Rig.Parents.SetNum(Num);
	for (int32 Bone = 0; Bone < Num; ++Bone)
	{
		Rig.Parents[Bone] = Skeleton.GetParentIndex(Bone);
	}
	Rig.Head = Skeleton.FindBoneIndex(TEXT("head"));
	static const TCHAR* const Sides[2] = { TEXT("r"), TEXT("l") };
	bool bArms = true;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		Rig.UpperArm[Side] = Skeleton.FindBoneIndex(*FString::Printf(TEXT("upperarm_%s"), Sides[Side]));
		Rig.LowerArm[Side] = Skeleton.FindBoneIndex(*FString::Printf(TEXT("lowerarm_%s"), Sides[Side]));
		Rig.Hand[Side] = Skeleton.FindBoneIndex(*FString::Printf(TEXT("hand_%s"), Sides[Side]));
		// The solve sets the three bones one below the other
		bArms &= Rig.UpperArm[Side] != INDEX_NONE && Rig.LowerArm[Side] != INDEX_NONE && Rig.Hand[Side] != INDEX_NONE
			&& Rig.Parents[Rig.LowerArm[Side]] == Rig.UpperArm[Side] && Rig.Parents[Rig.Hand[Side]] == Rig.LowerArm[Side];
	}
	Rig.bValid = bArms && Rig.Head != INDEX_NONE;
	return Rig;
}

bool HCGesture::UsesArms(EHCEmote Emote)
{
	return Emote != EHCEmote::Yes && Emote != EHCEmote::No;
}

void HCGesture::Apply(const FHCGestureRig& Rig, TArray<FTransform>& Pose, EHCEmote Emote, float Time, const FTransform& ActorToBody)
{
	using namespace HCGestureDetail;
	if (!Rig.bValid || Pose.Num() != Rig.Parents.Num() || Time < 0.f || Time > Seconds)
	{
		return;
	}
	const float W = Weight(Time);
	const float Wave = FMath::Sin(2.f * PI * 1.7f * Time);
	FArmGoal Arms[2];
	float Nod = 0.f, Shake = 0.f, Tilt = 0.f;
	switch (Emote)
	{
	case EHCEmote::Wave:
		Arms[0] = { true, FVector(10.f, 24.f + 10.f * Wave, 42.f), FVector(0.f, 1.f, -0.6f), 90.f, 10.f };
		break;
	case EHCEmote::ComeHere:
	{
		const float Pull = 0.5f + 0.5f * FMath::Sin(2.f * PI * 1.5f * Time);
		Arms[0] = { true, FVector(36.f - 14.f * Pull, 8.f, -14.f + 10.f * Pull), FVector(0.f, 0.3f, -1.f), -80.f, 25.f * Pull };
		break;
	}
	case EHCEmote::Point:
		Arms[0] = { true, FVector(56.f, 10.f, 4.f), FVector(0.f, 0.6f, -1.f), 0.f, 0.f };
		break;
	case EHCEmote::Stop:
		Arms[0] = { true, FVector(42.f, 6.f, 6.f), FVector(0.f, 0.5f, -1.f), 0.f, 70.f };
		break;
	case EHCEmote::Yes:
		Nod = 15.f * FMath::Sin(2.f * PI * 2.f * Time);
		break;
	case EHCEmote::No:
		Shake = 24.f * FMath::Sin(2.f * PI * 1.8f * Time);
		break;
	case EHCEmote::Shrug:
		Arms[0] = { true, FVector(26.f, 30.f, -20.f), FVector(-0.4f, 1.f, -0.8f), -90.f, 0.f };
		Arms[1] = Arms[0];
		Tilt = 9.f;
		break;
	case EHCEmote::HandsUp:
		Arms[0] = { true, FVector(8.f, 24.f, 44.f), FVector(0.f, 1.f, -0.5f), 90.f, 0.f };
		Arms[1] = Arms[0];
		break;
	default:
		return;
	}

	// Work on parent-relative transforms, then build the whole pose again from them
	TArray<FTransform> LocalArray;
	LocalArray.SetNum(Pose.Num());
	for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
	{
		const int32 Parent = Rig.Parents[Bone];
		LocalArray[Bone] = Parent != INDEX_NONE ? Pose[Bone].GetRelativeTransform(Pose[Parent]) : Pose[Bone];
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (Arms[Side].bActive)
		{
			SolveArm(Rig, Side, Pose, LocalArray, Arms[Side], ActorToBody, W);
		}
	}
	if (Nod != 0.f || Shake != 0.f || Tilt != 0.f)
	{
		const FVector Right = ActorToBody.TransformVectorNoScale(FVector::RightVector);
		const FVector Up = ActorToBody.TransformVectorNoScale(FVector::UpVector);
		const FVector Forward = ActorToBody.TransformVectorNoScale(FVector::ForwardVector);
		const FQuat Turn = FQuat(Right, FMath::DegreesToRadians(Nod * W)) * FQuat(Up, FMath::DegreesToRadians(Shake * W)) * FQuat(Forward, FMath::DegreesToRadians(Tilt * W));
		const int32 Parent = Rig.Parents[Rig.Head];
		const FQuat ParentRotation = Parent != INDEX_NONE ? Pose[Parent].GetRotation() : FQuat::Identity;
		LocalArray[Rig.Head].SetRotation((ParentRotation.Inverse() * Turn * Pose[Rig.Head].GetRotation()).GetNormalized());
	}
	for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
	{
		const int32 Parent = Rig.Parents[Bone];
		if (Parent != INDEX_NONE)
		{
			Pose[Bone] = LocalArray[Bone] * Pose[Parent];
		}
	}
}

FText HCGesture::Name(EHCEmote Emote)
{
	switch (Emote)
	{
	case EHCEmote::Wave: return NSLOCTEXT("HC", "EmoteWave", "Winken");
	case EHCEmote::ComeHere: return NSLOCTEXT("HC", "EmoteComeHere", "Komm mit");
	case EHCEmote::Point: return NSLOCTEXT("HC", "EmotePoint", "Dort!");
	case EHCEmote::Stop: return NSLOCTEXT("HC", "EmoteStop", "Halt");
	case EHCEmote::Yes: return NSLOCTEXT("HC", "EmoteYes", "Ja");
	case EHCEmote::No: return NSLOCTEXT("HC", "EmoteNo", "Nein");
	case EHCEmote::Shrug: return NSLOCTEXT("HC", "EmoteShrug", "Keine Ahnung");
	case EHCEmote::HandsUp: return NSLOCTEXT("HC", "EmoteHandsUp", "Ich war's nicht");
	default: return FText::GetEmpty();
	}
}

FText HCGesture::Shout(EHCEmote Emote)
{
	switch (Emote)
	{
	case EHCEmote::Wave: return NSLOCTEXT("HC", "EmoteWaveShout", "HALLO!");
	case EHCEmote::ComeHere: return NSLOCTEXT("HC", "EmoteComeHereShout", "KOMM MIT!");
	case EHCEmote::Point: return NSLOCTEXT("HC", "EmotePointShout", "DORT!");
	case EHCEmote::Stop: return NSLOCTEXT("HC", "EmoteStopShout", "HALT!");
	case EHCEmote::Yes: return NSLOCTEXT("HC", "EmoteYesShout", "JA");
	case EHCEmote::No: return NSLOCTEXT("HC", "EmoteNoShout", "NEIN");
	case EHCEmote::Shrug: return NSLOCTEXT("HC", "EmoteShrugShout", "KEINE AHNUNG");
	case EHCEmote::HandsUp: return NSLOCTEXT("HC", "EmoteHandsUpShout", "ICH WAR'S NICHT!");
	default: return FText::GetEmpty();
	}
}
