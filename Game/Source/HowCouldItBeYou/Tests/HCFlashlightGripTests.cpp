#include "Misc/AutomationTest.h"
#include "Characters/HCFlashlightGrip.h"
#include "ReferenceSkeleton.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCFlashlightGripTest
{
	/** A right arm with the proportions of the player figure (component space, cm), every bone unturned. */
	static void MakeArm(FReferenceSkeleton& Skeleton)
	{
		struct FBone { const TCHAR* Name; const TCHAR* Parent; FVector At; };
		static const FBone Bones[] = {
			{ TEXT("root"), nullptr, FVector(0., 0., 0.) },
			{ TEXT("head"), TEXT("root"), FVector(0., 0.7, 162.6) },
			{ TEXT("upperarm_r"), TEXT("root"), FVector(-19.0, -2.6, 143.6) },
			{ TEXT("lowerarm_r"), TEXT("upperarm_r"), FVector(-35.0, -1.9, 120.9) },
			{ TEXT("lowerarm_twist_01_r"), TEXT("lowerarm_r"), FVector(-43.5, 9.8, 109.9) },
			{ TEXT("hand_r"), TEXT("lowerarm_r"), FVector(-47.77, 15.70, 104.47) },
			{ TEXT("thumb_01_r"), TEXT("hand_r"), FVector(-46.23, 18.50, 102.96) },
			{ TEXT("thumb_02_r"), TEXT("thumb_01_r"), FVector(-45.19, 22.22, 100.91) },
			{ TEXT("thumb_03_r"), TEXT("thumb_02_r"), FVector(-45.05, 24.23, 98.57) },
			{ TEXT("index_01_r"), TEXT("hand_r"), FVector(-50.53, 23.73, 99.68) },
			{ TEXT("index_02_r"), TEXT("index_01_r"), FVector(-50.99, 25.94, 96.28) },
			{ TEXT("index_03_r"), TEXT("index_02_r"), FVector(-50.81, 26.93, 93.89) },
			{ TEXT("middle_01_r"), TEXT("hand_r"), FVector(-52.65, 21.98, 99.22) },
			{ TEXT("middle_02_r"), TEXT("middle_01_r"), FVector(-52.97, 24.27, 94.60) },
			{ TEXT("middle_03_r"), TEXT("middle_02_r"), FVector(-52.39, 24.88, 92.28) },
			{ TEXT("ring_01_r"), TEXT("hand_r"), FVector(-53.69, 19.78, 98.72) },
			{ TEXT("ring_02_r"), TEXT("ring_01_r"), FVector(-54.47, 21.69, 94.19) },
			{ TEXT("ring_03_r"), TEXT("ring_02_r"), FVector(-54.15, 22.25, 92.01) },
			{ TEXT("pinky_01_r"), TEXT("hand_r"), FVector(-53.54, 17.63, 97.97) },
			{ TEXT("pinky_02_r"), TEXT("pinky_01_r"), FVector(-54.55, 18.76, 94.47) },
			{ TEXT("pinky_03_r"), TEXT("pinky_02_r"), FVector(-54.36, 19.20, 92.48) },
		};
		FReferenceSkeletonModifier Modifier(Skeleton, nullptr);
		TMap<FName, TPair<int32, FVector>> Known;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
		{
			const TPair<int32, FVector>* Parent = Bones[Index].Parent ? Known.Find(Bones[Index].Parent) : nullptr;
			Modifier.Add(FMeshBoneInfo(Bones[Index].Name, Bones[Index].Name, Parent ? Parent->Key : INDEX_NONE),
				FTransform(Bones[Index].At - (Parent ? Parent->Value : FVector::ZeroVector)));
			Known.Add(Bones[Index].Name, TPair<int32, FVector>(Index, Bones[Index].At));
		}
	}

	static double FromHandle(const FTransform& Flashlight, const FVector& Point)
	{
		return FMath::PointDistToLine(Point, Flashlight.GetUnitAxis(EAxis::X), Flashlight.GetLocation());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCFlashlightGripTest, "HowCouldItBeYou.Flashlight.HandGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter)
bool FHCFlashlightGripTest::RunTest(const FString& Parameters)
{
	FReferenceSkeleton Skeleton;
	HCFlashlightGripTest::MakeArm(Skeleton);
	HCFlashlightGrip::FHandle Handle;
	Handle.Radius = 2.1f;
	Handle.BehindCentre = 1.5f;
	const FHCFlashlightGrip Grip = HCFlashlightGrip::Build(Skeleton, false, Handle);
	TestTrue(TEXT("A skeleton with a right arm and fingers gives a grip"), Grip.bValid);
	TestFalse(TEXT("No left arm, no left grip"), HCFlashlightGrip::Build(Skeleton, true, Handle).bValid);
	if (!Grip.bValid)
	{
		return false;
	}

	// Light off: the arm stays as it is, the fingers lie on the handle
	TArray<FTransform> Pose = Grip.RefPose;
	HCFlashlightGrip::Apply(Grip, Pose, nullptr, FVector::DownVector, 0.f);
	TestTrue(TEXT("Closing the fingers leaves the wrist where it was"), Pose[Grip.Hand].Equals(Grip.RefPose[Grip.Hand], 0.001));
	const FTransform Held = HCFlashlightGrip::FlashlightIn(Grip, Pose);
	for (const TCHAR* Finger : { TEXT("index_03_r"), TEXT("middle_03_r"), TEXT("ring_03_r"), TEXT("pinky_03_r"), TEXT("thumb_03_r") })
	{
		const int32 Bone = Skeleton.FindBoneIndex(Finger);
		const double Closed = HCFlashlightGripTest::FromHandle(Held, Pose[Bone].GetLocation());
		const double Open = HCFlashlightGripTest::FromHandle(Held, Grip.RefPose[Bone].GetLocation());
		TestTrue(FString::Printf(TEXT("%s lies on the handle, not in it (%.2f cm from its middle)"), Finger, Closed), Closed > Handle.Radius && Closed < Handle.Radius + 2.5);
		TestTrue(FString::Printf(TEXT("%s is not further from the handle than with an open hand (%.2f, open %.2f)"), Finger, Closed, Open), Closed <= Open + 0.3);
		const int32 Before = Skeleton.GetParentIndex(Bone);
		TestEqual(FString::Printf(TEXT("%s keeps its length"), Finger), FVector::Dist(Pose[Bone].GetLocation(), Pose[Before].GetLocation()),
			FVector::Dist(Grip.RefPose[Bone].GetLocation(), Grip.RefPose[Before].GetLocation()), 0.01);
	}

	// Light on: the flashlight ends up exactly where it is asked for, and the arm keeps its lengths
	const FVector Shoulder = Grip.RefPose[Grip.UpperArm].GetLocation();
	const FTransform Target(FRotator(10.f, 90.f, 0.f), Shoulder + FVector(2., 38., -28.));
	Pose = Grip.RefPose;
	HCFlashlightGrip::Apply(Grip, Pose, &Target, FVector(-0.5, -0.3, -1.), 1.f);
	const FTransform Raised = HCFlashlightGrip::FlashlightIn(Grip, Pose);
	TestTrue(FString::Printf(TEXT("Raised: the flashlight is at its target (%.2f cm off)"), FVector::Dist(Raised.GetLocation(), Target.GetLocation())),
		Raised.GetLocation().Equals(Target.GetLocation(), 0.05));
	TestTrue(TEXT("Raised: the flashlight points along its target"), (Raised.GetUnitAxis(EAxis::X) | Target.GetUnitAxis(EAxis::X)) > 0.9999);
	TestEqual(TEXT("The upper arm keeps its length"), FVector::Dist(Pose[Grip.LowerArm].GetLocation(), Pose[Grip.UpperArm].GetLocation()),
		FVector::Dist(Grip.RefPose[Grip.LowerArm].GetLocation(), Shoulder), 0.01);
	TestEqual(TEXT("The forearm keeps its length"), FVector::Dist(Pose[Grip.Hand].GetLocation(), Pose[Grip.LowerArm].GetLocation()),
		FVector::Dist(Grip.RefPose[Grip.Hand].GetLocation(), Grip.RefPose[Grip.LowerArm].GetLocation()), 0.01);
	TestTrue(TEXT("The shoulder does not move"), Pose[Grip.UpperArm].GetLocation().Equals(Shoulder, 0.001));
	TestTrue(TEXT("The elbow points down"), Pose[Grip.LowerArm].GetLocation().Z < Shoulder.Z - 10.);

	// Not raised at all: a target changes nothing about the arm
	Pose = Grip.RefPose;
	HCFlashlightGrip::Apply(Grip, Pose, &Target, FVector::DownVector, 0.f);
	TestTrue(TEXT("Raise 0 leaves the arm alone"), Pose[Grip.Hand].Equals(Grip.RefPose[Grip.Hand], 0.001) && Pose[Grip.LowerArm].Equals(Grip.RefPose[Grip.LowerArm], 0.001));

	// Halfway up: the wrist is somewhere between
	const FVector WristUp = (Grip.FlashlightInHand.Inverse() * Target).GetLocation();
	Pose = Grip.RefPose;
	HCFlashlightGrip::Apply(Grip, Pose, &Target, FVector(-0.5, -0.3, -1.), 0.5f);
	const double Whole = FVector::Dist(Grip.RefPose[Grip.Hand].GetLocation(), WristUp);
	TestTrue(TEXT("Raise 0.5 is on the way"), FVector::Dist(Pose[Grip.Hand].GetLocation(), WristUp) < Whole * 0.8 && FVector::Dist(Pose[Grip.Hand].GetLocation(), Grip.RefPose[Grip.Hand].GetLocation()) < Whole * 0.8);

	// A target out of reach: the arm stretches towards it, it does not tear
	const FTransform Far(FRotator(0.f, 90.f, 0.f), Shoulder + FVector(0., 200., 0.));
	Pose = Grip.RefPose;
	HCFlashlightGrip::Apply(Grip, Pose, &Far, FVector::DownVector, 1.f);
	TestEqual(TEXT("Out of reach: the forearm keeps its length"), FVector::Dist(Pose[Grip.Hand].GetLocation(), Pose[Grip.LowerArm].GetLocation()),
		FVector::Dist(Grip.RefPose[Grip.Hand].GetLocation(), Grip.RefPose[Grip.LowerArm].GetLocation()), 0.01);
	TestTrue(TEXT("Out of reach: the hand goes towards the target"), Pose[Grip.Hand].GetLocation().Y > Shoulder.Y + 40.);

	// The arm of the own view: everything else is gone
	HCFlashlightGrip::KeepOnlyArm(Grip, Pose);
	TestTrue(TEXT("Only the arm is left: the head is shrunk away"), Pose[Skeleton.FindBoneIndex(TEXT("head"))].GetScale3D().IsNearlyZero());
	TestTrue(TEXT("Only the arm is left: the hand is still there"), Pose[Grip.Hand].GetScale3D().Equals(FVector::OneVector));
	return true;
}

#endif
