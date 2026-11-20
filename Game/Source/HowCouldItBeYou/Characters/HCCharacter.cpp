#include "Characters/HCCharacter.h"
#include "Core/HCGameInstance.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "Characters/HCUneaseComponent.h"
#include "Characters/HCHandedness.h"
#include "Characters/HCBodyMeshComponent.h"
#include "Characters/HCFlashlightGrip.h"
#include "World/HCModelAssets.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "UI/HCHUD.h"

#include "Abilities/HCKillComponent.h"
#include "Audio/HCProceduralAudio.h"
#include "Camera/CameraComponent.h"
#include "Core/HCPlayerState.h"
#include "UObject/SoftObjectPath.h"
#include "Characters/HCCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCInputConfig.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputActionValue.h"
#include "Interaction/HCInteractionComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCStationBuilder.h"

namespace HCFlashlightBeam
{
	constexpr float Candela = 900.f;
	/** Closer than this to a surface the beam gets dimmer, so that it never becomes a blinding white disc. */
	constexpr float FullBrightFrom = 150.f;
	/** The light never starts closer than this to a wall in front of it. */
	constexpr float KeepAway = 35.f;
}

AHCCharacter::AHCCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UHCCharacterMovementComponent>(ACharacter::CharacterMovementComponentName)
		.SetDefaultSubobjectClass<UHCBodyMeshComponent>(ACharacter::MeshComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule 40 / 90 (docs/04 §2.1); players don't block the interaction trace
	GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Ignore);
	// Pawn against pawn only overlaps (docs/04 §3.3): nobody can body-block a door or push someone off a task
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// Template skeletal mesh is not used in the greybox prototype
	GetMesh()->SetVisibility(false);
	GetMesh()->SetReceivesDecals(false); // no blood on the feet of whoever stands in it
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Feet on the floor, looking along the capsule. Set here (not later) because the movement smoothing of other
	// players' figures remembers this offset when the components are initialised.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(10.f, 0.f, 70.f)); // eyes at ~160 cm
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(90.f);

	auto MakeBodyPart = [this](const TCHAR* PartName, UStaticMesh* PartMesh, const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(GetCapsuleComponent());
		Part->SetStaticMesh(PartMesh);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetOwnerNoSee(true);
		Part->SetCanEverAffectNavigation(false);
		Part->bCastHiddenShadow = true;
		return Part;
	};
	// Body 160 cm cylinder (feet at -90), head sphere on top, dark visor facing forward
	BodyMesh = MakeBodyPart(TEXT("BodyMesh"), HCGreybox::Cylinder(), FVector(0.f, 0.f, -15.f), FVector(0.7f, 0.7f, 1.5f));
	HeadMesh = MakeBodyPart(TEXT("HeadMesh"), HCGreybox::Sphere(), FVector(0.f, 0.f, 68.f), FVector(0.42f, 0.42f, 0.44f));
	VisorMesh = MakeBodyPart(TEXT("VisorMesh"), HCGreybox::Cube(), FVector(16.f, 0.f, 70.f), FVector(0.12f, 0.3f, 0.12f));

	// Dark plates with the name on chest and back
	ChestPlate = MakeBodyPart(TEXT("ChestPlate"), HCGreybox::Cube(), FVector(34.5f, 0.f, 22.f), FVector(0.03f, 0.46f, 0.15f));
	BackPlate = MakeBodyPart(TEXT("BackPlate"), HCGreybox::Cube(), FVector(-34.5f, 0.f, 30.f), FVector(0.03f, 0.58f, 0.19f));
	auto MakeBadge = [this](const TCHAR* BadgeName, const FVector& Location, float Yaw, float Size)
	{
		UTextRenderComponent* Badge = CreateDefaultSubobject<UTextRenderComponent>(BadgeName);
		Badge->SetupAttachment(GetCapsuleComponent());
		Badge->SetRelativeLocation(Location);
		Badge->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		Badge->SetHorizontalAlignment(EHTA_Center);
		Badge->SetVerticalAlignment(EVRTA_TextCenter);
		Badge->SetWorldSize(Size);
		Badge->SetTextRenderColor(FColor(235, 235, 235));
		Badge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Badge->SetOwnerNoSee(true);
		return Badge;
	};
	ChestLabel = MakeBadge(TEXT("ChestLabel"), FVector(36.5f, 0.f, 22.f), 0.f, 11.f);
	BackLabel = MakeBadge(TEXT("BackLabel"), FVector(-36.5f, 0.f, 30.f), 180.f, 14.f);
	NameTag = MakeBadge(TEXT("NameTag"), FVector(0.f, 0.f, 112.f), 0.f, 13.f);

	ShapeshiftComponent = CreateDefaultSubobject<UHCShapeshiftComponent>(TEXT("Shapeshift"));
	UneaseComponent = CreateDefaultSubobject<UHCUneaseComponent>(TEXT("Unease"));

	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(GetCapsuleComponent());
	Flashlight->SetRelativeLocation(FVector(20.f, 12.f, 55.f));
	Flashlight->SetIntensityUnits(ELightUnits::Candelas);
	Flashlight->SetIntensity(HCFlashlightBeam::Candela);
	Flashlight->SetAttenuationRadius(1500.f);
	Flashlight->SetInnerConeAngle(10.f);
	Flashlight->SetOuterConeAngle(24.f);
	Flashlight->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f));
	Flashlight->SetCastShadows(true);
	Flashlight->SetUsingAbsoluteRotation(true);
	Flashlight->SetVisibility(false);
	CreateExtraComponents();

	HelmetLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HelmetLight"));
	HelmetLight->SetupAttachment(GetCapsuleComponent());
	HelmetLight->SetRelativeLocation(FVector(25.f, 0.f, 80.f));
	HelmetLight->SetIntensityUnits(ELightUnits::Candelas);
	HelmetLight->SetIntensity(2.f);
	HelmetLight->SetAttenuationRadius(300.f);
	HelmetLight->SetLightColor(FLinearColor(0.7f, 0.85f, 1.f));
	HelmetLight->SetCastShadows(false);

	// The flashlight model (if set up): the own one in the lower right of the view, the others' in their hand
	FlashlightGrip = CreateDefaultSubobject<USceneComponent>(TEXT("FlashlightGrip"));
	FlashlightGrip->SetupAttachment(Camera);
	FlashlightViewModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlashlightViewModel"));
	FlashlightViewModel->SetupAttachment(FlashlightGrip);
	FlashlightViewModel->SetOnlyOwnerSee(true);
	FlashlightHandModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlashlightHandModel"));
	FlashlightHandModel->SetupAttachment(GetCapsuleComponent());
	FlashlightHandModel->SetOwnerNoSee(true);
	for (UStaticMeshComponent* Model : { FlashlightViewModel.Get(), FlashlightHandModel.Get() })
	{
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->SetCanEverAffectNavigation(false);
		Model->SetReceivesDecals(false);
		Model->SetVisibility(false);
	}
	// The own flashlight sits right at the eye: its shadow would only be a dark smear in the beam
	FlashlightViewModel->SetCastShadow(false);
	// The arm that holds it (gets the body of the real model in ApplyRealModel)
	FlashlightViewArm = CreateDefaultSubobject<UHCViewArmComponent>(TEXT("FlashlightViewArm"));
	FlashlightViewArm->SetupAttachment(Camera);
	FlashlightViewArm->SetOnlyOwnerSee(true);
	FlashlightViewArm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlashlightViewArm->SetCanEverAffectNavigation(false);
	FlashlightViewArm->SetReceivesDecals(false);
	FlashlightViewArm->SetCastShadow(false);
	FlashlightViewArm->SetVisibility(false);
	FlashlightViewArm->BoundsScale = 3.f; // its pose has little to do with the body it is cut from

	Interaction =CreateDefaultSubobject<UHCInteractionComponent>(TEXT("Interaction"));
	// Crew and monster have exactly the same components (docs/02 §4.2)
	KillComponent = CreateDefaultSubobject<UHCKillComponent>(TEXT("Kill"));

	MonsterMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MonsterMarker"));
	MonsterMarker->SetupAttachment(GetCapsuleComponent());
	MonsterMarker->SetStaticMesh(Cast<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone")).TryLoad()));
	MonsterMarker->SetRelativeLocation(FVector(0.f, 0.f, 146.f));
	MonsterMarker->SetRelativeRotation(FRotator(180.f, 0.f, 0.f)); // tip down
	MonsterMarker->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.28f));
	MonsterMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MonsterMarker->SetCanEverAffectNavigation(false);
	MonsterMarker->SetCastShadow(false);
	MonsterMarker->SetVisibility(false);
}

UHCCharacterMovementComponent* AHCCharacter::GetHCMovement() const
{
	return Cast<UHCCharacterMovementComponent>(GetCharacterMovement());
}

void AHCCharacter::BeginPlay()
{
	Super::BeginPlay();

	const UHCGameSettings* Settings = UHCGameSettings::Get();
	Flashlight->SetAttenuationRadius(Settings->FlashlightRange);
	HelmetLight->SetAttenuationRadius(Settings->HelmetLightRange);

	BodyMaterial = HCGreybox::MakeColored(this, BodyColor);
	if (BodyMaterial)
	{
		BodyMesh->SetMaterial(0, BodyMaterial);
		HeadMesh->SetMaterial(0, BodyMaterial);
	}
	if (UMaterialInstanceDynamic* VisorMat = HCGreybox::MakeColored(this, FLinearColor(0.02f, 0.02f, 0.03f)))
	{
		VisorMesh->SetMaterial(0, VisorMat);
		ChestPlate->SetMaterial(0, VisorMat);
		BackPlate->SetMaterial(0, VisorMat);
	}
	if (UMaterialInstanceDynamic* MarkerMaterial = HCGreybox::MakeColored(this, FLinearColor(1.f, 0.02f, 0.02f)))
	{
		MonsterMarker->SetMaterial(0, MarkerMaterial);
	}
	// The name hangs above the head now; the badges on chest and back are no longer shown
	for (USceneComponent* Old : { static_cast<USceneComponent*>(ChestLabel.Get()), static_cast<USceneComponent*>(BackLabel.Get()),
		static_cast<USceneComponent*>(ChestPlate.Get()), static_cast<USceneComponent*>(BackPlate.Get()) })
	{
		Old->SetVisibility(false);
	}
	ApplyRealModel();
	ApplyFlashlightModel();
	OnRep_FlashlightOn();
	OnRep_Appearance();
}

namespace HCHeldFlashlight
{
	/** Length of the model in the game (a big handheld torch). */
	constexpr float Length = 24.f;
	/** Centre of the own flashlight relative to the eye: forward, right, down (cm); mirrored for the left hand. */
	static FVector ViewCentre(bool bLeft) { return FVector(41.f, bLeft ? -17.f : 17.f, -16.f); }
	/** Pointing a little inwards and up, towards the middle of the view. */
	static FRotator ViewTurn(bool bLeft) { return FRotator(2.f, bLeft ? 5.f : -5.f, 0.f); }
	static FName HandBone(bool bLeft) { static const FName Left(TEXT("hand_l")), Right(TEXT("hand_r")); return bLeft ? Left : Right; }
	/** Where the beam starts without a flashlight model: at the chest, on the side of the hand. */
	static FVector BareBeam(bool bLeft) { return FVector(20.f, bLeft ? -12.f : 12.f, 55.f); }

	/** The part of the model the hand closes around: radius, and how far behind the middle the hand holds it (cm). */
	constexpr float HandleRadius = 2.1f;
	constexpr float GripBehindCentre = 1.5f;
	/** Seconds the arm takes to come up when the light goes on (and to sink when it goes out). */
	constexpr float RaiseSeconds = 0.28f;
	static float Eased(float Raise) { return FMath::InterpEaseInOut(0.f, 1.f, Raise, 2.f); }
	/** Own view, light off: the hand is down, out of the picture, the flashlight pointing at the floor ahead. */
	static FVector LowCentre(bool bLeft) { return FVector(38.f, bLeft ? -19.f : 19.f, -31.f); }
	static FRotator LowTurn(bool bLeft) { return FRotator(-30.f, bLeft ? 3.f : -3.f, 0.f); }
	/** Own view: the shoulder of the arm, relative to the eye (low, so that the forearm comes up from below). */
	static FVector ViewShoulder(bool bLeft) { return FVector(2.f, bLeft ? -26.f : 26.f, -33.f); }
	static FVector ViewElbow(bool bLeft) { return FVector(0.2f, bLeft ? -0.15f : 0.15f, -1.f); }
	/** For the others, light on: the fist in front of the shoulder. The middle of the flashlight seen from the
	 *  shoulder joint (forward, right, up), looking level; it swings around the joint when the player looks up or down. */
	static FVector RaisedFromShoulder(bool bLeft) { return FVector(23.5f, bLeft ? -1.f : 1.f, -5.5f); }
	static FVector RaisedElbow(bool bLeft) { return FVector(0.3f, bLeft ? -0.3f : 0.3f, -1.f); }
}

void AHCCharacter::ApplyFlashlightModel()
{
	const UHCModelSet* Set = HCModels::Get();
	UStaticMesh* Model = Set ? Set->FlashlightMesh.Get() : nullptr;
	if (!Model || GetNetMode() == NM_DedicatedServer)
	{
		return; // no model: the beam just comes out of the chest as before
	}
	// Lens towards +X, centred, scaled to the length of a real torch
	const FQuat Turn = Set->FlashlightRotation.Quaternion();
	const FBox Box = Model->GetBoundingBox();
	const FBox Turned = Box.TransformBy(FTransform(Turn));
	const float Scale = HCHeldFlashlight::Length / FMath::Max(0.1f, static_cast<float>(Turned.GetSize().X));
	FlashlightFit = FTransform(Turn, -Turn.RotateVector(Box.GetCenter()) * Scale, FVector(Scale));
	FlashlightLensInModel = FlashlightFit.InverseTransformPosition(FVector(HCHeldFlashlight::Length * 0.5f + 1.5f, 0.f, 0.f));

	FlashlightLensMaterials.Reset();
	for (UStaticMeshComponent* Held : { FlashlightViewModel.Get(), FlashlightHandModel.Get() })
	{
		Held->SetStaticMesh(Model);
		const int32 LensSlot = Set->FlashlightLensSlot.IsNone() ? INDEX_NONE : Held->GetMaterialIndex(Set->FlashlightLensSlot);
		if (UMaterialInstanceDynamic* Lens = LensSlot != INDEX_NONE ? Held->CreateDynamicMaterialInstance(LensSlot) : nullptr)
		{
			FlashlightLensMaterials.Add(Lens);
		}
	}
	FlashlightViewModel->SetRelativeTransform(FlashlightFit);
	FlashlightGrip->SetRelativeLocationAndRotation(HCHeldFlashlight::ViewCentre(bFlashlightLeftHand), HCHeldFlashlight::ViewTurn(bFlashlightLeftHand));
	// How each hand closes around the handle, worked out from the skeleton of the real figure
	if (bRealModel && Models && Models->CharacterMesh)
	{
		HCFlashlightGrip::FHandle Handle;
		Handle.Radius = HCHeldFlashlight::HandleRadius;
		Handle.BehindCentre = HCHeldFlashlight::GripBehindCentre;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FlashlightGrips[Side] = HCFlashlightGrip::Build(Models->CharacterMesh->GetRefSkeleton(), Side == 1, Handle);
		}
	}
	bFlashlightHandBones = FlashlightGrips[0].bValid && FlashlightGrips[1].bValid;
	bFlashlightModel = true;
	AttachHandFlashlight();
	bFlashlightLensOn = true; // forces the first UpdateFlashlightLens to write the parameter
	UpdateFlashlightLens();
}

void AHCCharacter::UpdateFlashlightLens()
{
	const bool bOn = bFlashlightOn && bFlashlightModel;
	if (bOn == bFlashlightLensOn)
	{
		return;
	}
	bFlashlightLensOn = bOn;
	for (UMaterialInstanceDynamic* Lens : FlashlightLensMaterials)
	{
		Lens->SetScalarParameterValue(TEXT("Glow"), bOn ? 1.f : 0.f);
	}
}

void AHCCharacter::AttachHandFlashlight()
{
	if (!bFlashlightModel || !bFlashlightHandBones)
	{
		return;
	}
	const FHCFlashlightGrip& Grip = FlashlightGrips[bFlashlightLeftHand ? 1 : 0];
	// For the others: fixed in the fist, it goes wherever the hand goes
	FlashlightHandModel->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, HCHeldFlashlight::HandBone(bFlashlightLeftHand));
	FlashlightHandModel->SetRelativeTransform(FlashlightFit * Grip.FlashlightInHand);
	// Own view: the model looks along its +Y, so it is turned into the view and moved until its shoulder sits
	// where the player's own would be
	const FQuat IntoView = FRotator(0.f, -90.f, 0.f).Quaternion();
	FlashlightViewArm->SetRelativeLocationAndRotation(
		HCHeldFlashlight::ViewShoulder(bFlashlightLeftHand) - IntoView.RotateVector(Grip.RefPose[Grip.UpperArm].GetLocation()), IntoView);
}

void AHCCharacter::UpdateViewArm()
{
	const FHCFlashlightGrip& Grip = FlashlightGrips[bFlashlightLeftHand ? 1 : 0];
	const FTransform ArmSpace = FlashlightViewArm->GetComponentTransform();
	const FTransform HeldAt = FlashlightGrip->GetComponentTransform().GetRelativeTransform(ArmSpace);
	// The elbow hangs below the fist
	const FVector Elbow = ArmSpace.InverseTransformVectorNoScale(
		Camera->GetComponentTransform().TransformVectorNoScale(HCHeldFlashlight::ViewElbow(bFlashlightLeftHand)));
	FlashlightViewArm->ShowPose([&Grip, &HeldAt, &Elbow](TArray<FTransform>& Pose)
	{
		if (Pose.Num() == Grip.NumBones)
		{
			Pose = Grip.RefPose;
			HCFlashlightGrip::Apply(Grip, Pose, &HeldAt, Elbow, 1.f);
			HCFlashlightGrip::KeepOnlyArm(Grip, Pose);
		}
	});
}

void AHCCharacter::EditBodyPose(TArray<FTransform>& Pose)
{
	// Carrying a container needs both hands; otherwise one hand holds the flashlight. An emote goes on top of either.
	if (!bCarrying || !EditCarryPose(Pose))
	{
		EditFlashlightPose(Pose);
	}
	EditGesturePose(Pose);
}

void AHCCharacter::EditFlashlightPose(TArray<FTransform>& Pose)
{
	if (!bFlashlightModel || !bFlashlightHandBones || !bAlive || bMonsterForm || !Models || GetMesh()->GetSkeletalMeshAsset() != Models->CharacterMesh)
	{
		return;
	}
	const bool bLeft = bFlashlightLeftHand;
	const FHCFlashlightGrip& Grip = FlashlightGrips[bLeft ? 1 : 0];
	const float Raised = HCHeldFlashlight::Eased(FlashlightRaise);
	if (Raised <= 0.f)
	{
		// Light off: the arm does what the animation says, only the fingers stay closed around the handle
		HCFlashlightGrip::Apply(Grip, Pose, nullptr, FVector::DownVector, 0.f);
		return;
	}
	// Light on: held up in front of the shoulder, pointing exactly where the beam goes. Looking up or down swings
	// the arm around the shoulder - less far down than up, so that the fist stays in front of the chest.
	// The fist hangs on the shoulder of the animation, so it moves with the body's steps and breathing.
	const float Pitch = FRotator::NormalizeAxis(GetBaseAimRotation().Pitch);
	const FTransform ActorToBody = GetActorTransform().GetRelativeTransform(GetMesh()->GetComponentTransform());
	const FVector Shoulder = ActorToBody.InverseTransformPosition(Pose[Grip.UpperArm].GetLocation());
	const FVector At = Shoulder + FRotator(Pitch * (Pitch > 0.f ? 0.7f : 0.45f), 0.f, 0.f).RotateVector(HCHeldFlashlight::RaisedFromShoulder(bLeft));
	const FTransform HeldAt = FTransform(FRotator(Pitch, 0.f, 0.f), At) * ActorToBody;
	const FVector Elbow = ActorToBody.TransformVectorNoScale(HCHeldFlashlight::RaisedElbow(bLeft));
	HCFlashlightGrip::Apply(Grip, Pose, &HeldAt, Elbow, Raised);
}

void AHCCharacter::UpdateHeldFlashlight(float DeltaSeconds)
{
	// The arm comes up while the light is on and sinks again when it goes out
	FlashlightRaise = FMath::FInterpConstantTo(FlashlightRaise, bFlashlightOn ? 1.f : 0.f, DeltaSeconds, 1.f / HCHeldFlashlight::RaiseSeconds);
	if (!bFlashlightModel)
	{
		return;
	}
	const bool bHolding = bAlive && !bMonsterForm && !bCarrying;
	const bool bLocal = IsLocallyControlled();
	// In the wardrobe mirror the own figure is seen from outside: the light is in its hand, not in front of the eyes
	const bool bShowView = bHolding && bLocal && !bKillLocked && !bMirrorView;
	const bool bShowHand = bHolding && (!bLocal || bMirrorView) && bFlashlightHandBones;
	if (FlashlightViewModel->IsVisible() != bShowView)
	{
		FlashlightViewModel->SetVisibility(bShowView);
		FlashlightLastView = Camera->GetComponentRotation();
		FlashlightSway = FRotator::ZeroRotator;
	}
	if (FlashlightHandModel->IsVisible() != bShowHand)
	{
		FlashlightHandModel->SetVisibility(bShowHand);
	}
	const bool bShowArm = bShowView && bFlashlightHandBones;
	if (FlashlightViewArm->IsVisible() != bShowArm)
	{
		FlashlightViewArm->SetVisibility(bShowArm);
	}
	if (bShowView)
	{
		// The hand follows the view a little late ...
		const FRotator View = Camera->GetComponentRotation();
		const FRotator Turned = (View - FlashlightLastView).GetNormalized();
		FlashlightLastView = View;
		FlashlightSway.Pitch = FMath::Clamp(FlashlightSway.Pitch - Turned.Pitch * 0.3f, -4.f, 4.f);
		FlashlightSway.Yaw = FMath::Clamp(FlashlightSway.Yaw - Turned.Yaw * 0.3f, -5.f, 5.f);
		FlashlightSway = FMath::RInterpTo(FlashlightSway, FRotator::ZeroRotator, DeltaSeconds, 7.f);
		// ... and bobs with the steps
		const float Moving = FMath::Clamp(GetVelocity().Size2D() / FMath::Max(1.f, UHCGameSettings::Get()->WalkSpeed), 0.f, 1.6f);
		FlashlightBob = FMath::FInterpTo(FlashlightBob, Moving, DeltaSeconds, 5.f);
		FlashlightBobPhase = FMath::Fmod(FlashlightBobPhase + DeltaSeconds * 2.f * PI * (0.9f + 0.4f * FlashlightBob), 2.f * PI);
		const FVector Bob(0.f, 0.8f * FMath::Sin(FlashlightBobPhase), -0.7f * FMath::Abs(FMath::Sin(FlashlightBobPhase)));
		// Close to a wall the hand pulls the flashlight back instead of pushing it through
		const FVector Eye = Camera->GetComponentLocation();
		// Lowered while the light is off, up in the view while it is on
		const float Raised = HCHeldFlashlight::Eased(FlashlightRaise);
		const FVector Centre = FMath::Lerp(HCHeldFlashlight::LowCentre(bFlashlightLeftHand), HCHeldFlashlight::ViewCentre(bFlashlightLeftHand), Raised);
		const FRotator Turn = FMath::Lerp(HCHeldFlashlight::LowTurn(bFlashlightLeftHand), HCHeldFlashlight::ViewTurn(bFlashlightLeftHand), Raised);
		const FVector Tip = Camera->GetComponentTransform().TransformPosition(
			Centre + Turn.RotateVector(FVector(HCHeldFlashlight::Length * 0.5f + 3.f, 0.f, 0.f)));
		FHitResult Hit;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(HCFlashlightReach), false, this);
		const float Blocked = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Tip, ECC_Visibility, Query) ? static_cast<float>(FVector::Dist(Hit.Location, Tip)) : 0.f;
		FlashlightRetract = FMath::FInterpTo(FlashlightRetract, FMath::Min(Blocked, 30.f), DeltaSeconds, 12.f);
		FlashlightGrip->SetRelativeLocationAndRotation(Centre + Bob * FlashlightBob - FVector(FlashlightRetract, 0.f, 0.f), Turn + FlashlightSway);
		if (bShowArm)
		{
			UpdateViewArm();
		}
	}
	// For the others the flashlight is fixed in the fist (AttachHandFlashlight); EditBodyPose moves hand and arm
}

bool AHCCharacter::CanAct() const
{
	if (!bAlive || bKillLocked || bInVent || ShapeshiftComponent->IsChanging())
	{
		return false;
	}
	// Interactions only in the lobby and in a running round (docs/02 §10.4)
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const EHCMatchPhase Phase = GS ? GS->GetMatchPhase() : EHCMatchPhase::Lobby;
	return Phase == EHCMatchPhase::Lobby || Phase == EHCMatchPhase::Playing;
}

bool AHCCharacter::CanMove() const
{
	// The Wandler stands still while it changes shape (docs/02 §6.3)
	if (bKillLocked || bInVent || ShapeshiftComponent->IsChanging())
	{
		return false;
	}
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const EHCMatchPhase Phase = GS ? GS->GetMatchPhase() : EHCMatchPhase::Lobby;
	return Phase == EHCMatchPhase::Lobby || Phase == EHCMatchPhase::Playing;
}

float AHCCharacter::GetStaminaFraction() const
{
	const UHCCharacterMovementComponent* Move = GetHCMovement();
	return Move ? Move->GetStamina() : 1.f;
}

bool AHCCharacter::IsSprinting() const
{
	const UHCCharacterMovementComponent* Move = GetHCMovement();
	return Move && Move->IsSprinting();
}

void AHCCharacter::SetBodyColor(const FLinearColor& InColor)
{
	BodyColor = InColor;
	OnRep_BodyColor();
}

void AHCCharacter::OnRep_BodyColor()
{
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), BodyColor);
	}
	for (const TArray<TObjectPtr<UMaterialInstanceDynamic>>* List : { &TintMaterials, &ArmTintMaterials })
	{
		for (UMaterialInstanceDynamic* Tinted : *List)
		{
			if (Tinted && Models)
			{
				Tinted->SetVectorParameterValue(Models->TintParameter, BodyColor);
			}
		}
	}
}

void AHCCharacter::ApplyRealModel()
{
	Models = HCModels::Get();
	USkeletalMesh* RealMesh = Models ? Models->CharacterMesh.Get() : nullptr;
	if (!RealMesh)
	{
		return; // no model packs (e.g. a fresh clone of the repository): the greybox figure stays
	}
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetSkeletalMesh(RealMesh);
	// Epic convention: the model looks along +Y and stands on its origin; the capsule centre is 90 cm up
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	CacheInitialMeshOffset(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	Body->SetVisibility(true);
	Body->SetOwnerNoSee(true); // first person: nobody sees the inside of the own head
	Body->bCastHiddenShadow = true;
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (Models->CharacterAnimClass)
	{
		Body->SetAnimInstanceClass(Models->CharacterAnimClass);
	}
	for (UStaticMeshComponent* Part : { BodyMesh.Get(), HeadMesh.Get(), VisorMesh.Get(), ChestPlate.Get(), BackPlate.Get() })
	{
		Part->SetVisibility(false);
		Part->bCastHiddenShadow = false;
		Part->SetCastShadow(false);
	}
	TintMaterials.Reset();
	for (const FName Slot : Models->TintSlots)
	{
		const int32 SlotIndex = Body->GetMaterialIndex(Slot);
		if (UMaterialInstanceDynamic* Tinted = SlotIndex != INDEX_NONE ? Body->CreateDynamicMaterialInstance(SlotIndex) : nullptr)
		{
			TintMaterials.Add(Tinted);
		}
	}
	// The pose of every frame passes through EditBodyPose before it is shown
	if (UHCBodyMeshComponent* Posed = Cast<UHCBodyMeshComponent>(Body))
	{
		Posed->PoseEdit = [this](TArray<FTransform>& Pose) { EditBodyPose(Pose); };
	}
	// The arm of the own view is cut from the same body, with the same jacket
	if (GetNetMode() != NM_DedicatedServer)
	{
		FlashlightViewArm->SetSkinnedAssetAndUpdate(RealMesh);
		ArmTintMaterials.Reset();
		for (const FName Slot : Models->TintSlots)
		{
			const int32 SlotIndex = FlashlightViewArm->GetMaterialIndex(Slot);
			if (UMaterialInstanceDynamic* Tinted = SlotIndex != INDEX_NONE ? FlashlightViewArm->CreateDynamicMaterialInstance(SlotIndex) : nullptr)
			{
				ArmTintMaterials.Add(Tinted);
			}
		}
	}
	bRealModel = true;
}

void AHCCharacter::ServerSetMonsterForm(bool bOn, float InSpeedFactor)
{
	bMonsterForm = bOn;
	SpeedFactor = bOn ? FMath::Clamp(InSpeedFactor, 1.f, 3.f) : 1.f;
	OnRep_MonsterForm();
	ForceNetUpdate();
}

void AHCCharacter::OnRep_MonsterForm()
{
	if (!bNameTagColorKnown)
	{
		NameTagColor = NameTag->TextRenderColor;
		bNameTagColorKnown = true;
	}
	// The name stays the real one - in red while the monster shows itself
	NameTag->SetTextRenderColor(bMonsterForm ? FColor(255, 45, 30) : NameTagColor);
	if (!bRealModel || !Models || !Models->MonsterMesh)
	{
		return; // no model packs: only the red name tells
	}
	USkeletalMeshComponent* Body = GetMesh();
	// Materials set for one body must not end up on the other
	Body->EmptyOverrideMaterials();
	TintMaterials.Reset();
	MonsterAnim = nullptr;
	MonsterAnimLockedUntil = 0.f;
	if (bMonsterForm)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetSkeletalMesh(Models->MonsterMesh);
		UpdateMonsterAnimation();
	}
	else
	{
		Body->SetSkeletalMesh(Models->CharacterMesh);
		if (Models->CharacterAnimClass)
		{
			Body->SetAnimInstanceClass(Models->CharacterAnimClass);
		}
		for (const FName Slot : Models->TintSlots)
		{
			const int32 SlotIndex = Body->GetMaterialIndex(Slot);
			if (UMaterialInstanceDynamic* Tinted = SlotIndex != INDEX_NONE ? Body->CreateDynamicMaterialInstance(SlotIndex) : nullptr)
			{
				TintMaterials.Add(Tinted);
			}
		}
		OnRep_BodyColor();
		// The new body has fresh materials and shows every part again: trousers, hair, boots and backpack once more
		OutfitMaterials.Reset();
		ApplyOutfit();
	}
}

void AHCCharacter::UpdateMonsterAnimation()
{
	if (!bMonsterForm || !bRealModel || !Models || GetMesh()->GetSkeletalMeshAsset() != Models->MonsterMesh)
	{
		return;
	}
	if (GetWorld()->GetTimeSeconds() < MonsterAnimLockedUntil)
	{
		return; // an attack plays to its end
	}
	const float Speed = GetVelocity().Size2D();
	UAnimSequence* Wanted = Speed < 15.f ? Models->MonsterIdle.Get() : Speed < UHCGameSettings::Get()->WalkSpeed * 1.2f ? Models->MonsterWalk.Get() : Models->MonsterRun.Get();
	if (!Wanted)
	{
		Wanted = Models->MonsterIdle ? Models->MonsterIdle.Get() : Models->MonsterWalk.Get();
	}
	if (Wanted && Wanted != MonsterAnim)
	{
		MonsterAnim = Wanted;
		GetMesh()->PlayAnimation(Wanted, true);
	}
}

void AHCCharacter::Input_TrueForm()
{
	// Does nothing visible or audible unless this player is the Wandler
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!PS || PS->GetRoleInfo().Role != EHCRole::Monster || !IsLocallyControlled())
	{
		return;
	}
	if (ShapeshiftComponent->GetState() == EHCShapeshiftState::Disguised && ShapeshiftComponent->IsTrueForm())
	{
		ShapeshiftComponent->ServerRequestRevert();
	}
	else if (ShapeshiftComponent->GetState() == EHCShapeshiftState::Ready && CanAct())
	{
		ShapeshiftComponent->ServerRequestTrueForm();
	}
}

void AHCCharacter::ServerSetAppearance(int32 InIdentity, const FString& InName)
{
	Appearance.Identity = InIdentity;
	Appearance.DisplayName = InName;
	OnRep_Appearance();
}

void AHCCharacter::ServerSetAppearance(int32 InIdentity, const FString& InName, uint8 InOutfit)
{
	Appearance.Outfit = InOutfit;
	ServerSetAppearance(InIdentity, InName);
}

void AHCCharacter::OnRep_Appearance()
{
	if (Appearance.Identity != INDEX_NONE)
	{
		BodyColor = HCIdentity::Color(Appearance.Identity);
		OnRep_BodyColor();
	}
	// Long names are squeezed so that they stay on the badge
	const FText BadgeText = FText::FromString(Appearance.DisplayName);
	const float Squeeze = FMath::Min(1.f, 7.f / FMath::Max(1, Appearance.DisplayName.Len()));
	for (UTextRenderComponent* Label : { ChestLabel.Get(), BackLabel.Get(), NameTag.Get() })
	{
		Label->SetText(BadgeText);
		Label->SetXScale(Squeeze);
	}
	// Flo, Finn & co. hold the flashlight in the left hand - and so does a Wandler that looks like them
	bFlashlightLeftHand = HCHandedness::IsLeftHandedName(Appearance.DisplayName);
	Flashlight->SetRelativeLocation(HCHeldFlashlight::BareBeam(bFlashlightLeftHand));
	AttachHandFlashlight();
	ApplyOutfit();
}

void AHCCharacter::Input_Shapeshift()
{
	// Does nothing visible or audible unless this player is the Wandler (docs/01 §4.2)
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!PS || PS->GetRoleInfo().Role != EHCRole::Monster || !IsLocallyControlled())
	{
		return;
	}
	if (ShapeshiftComponent->GetState() == EHCShapeshiftState::Disguised)
	{
		ShapeshiftComponent->ServerRequestRevert();
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(GetController());
	AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr;
	if (HUD && ShapeshiftComponent->GetState() == EHCShapeshiftState::Ready && CanAct())
	{
		HUD->ToggleShapeshiftMenu();
	}
}

void AHCCharacter::Input_Map()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	AHCHUD* HUD = PC && IsLocallyControlled() ? PC->GetHUD<AHCHUD>() : nullptr;
	if (HUD && CanAct())
	{
		HUD->ToggleStationMap();
	}
}

void AHCCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMonsterAnimation();
	// Personal field of view from the settings menu
	if (IsLocallyControlled())
	{
		const UHCGameInstance* Instance = GetGameInstance<UHCGameInstance>();
		if (Instance && !FMath::IsNearlyEqual(Camera->FieldOfView, Instance->GetFieldOfView(), 0.1f))
		{
			Camera->SetFieldOfView(Instance->GetFieldOfView());
		}
	}
	// Turn the name tag towards whoever is looking on this machine
	if (const APlayerController* Viewer = GetNetMode() != NM_DedicatedServer ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
		NameTag->SetWorldRotation(FRotator(0.f, (ViewLocation - NameTag->GetComponentLocation()).Rotation().Yaw, 0.f));
	}
	if (HasAuthority())
	{
		UpdateBattery(DeltaSeconds);
	}
	UpdateHeldFlashlight(DeltaSeconds);
	UpdateFlashlightAim();
	UpdateKillAttack(DeltaSeconds);
	UpdateExtras(DeltaSeconds);
	if (GetWorld()->GetTimeSeconds() >= NextMarkerCheckTime)
	{
		NextMarkerCheckTime = GetWorld()->GetTimeSeconds() + 0.4f;
		UpdateMonsterMarker();
	}
}

void AHCCharacter::UpdateBattery(float DeltaSeconds)
{
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	if (bFlashlightOn)
	{
		Battery = FMath::Max(0.f, Battery - DeltaSeconds / FMath::Max(1.f, Settings->FlashlightBatterySeconds));
		if (Battery <= 0.f)
		{
			bFlashlightOn = false;
			OnRep_FlashlightOn();
		}
	}
	else if (Battery < 1.f)
	{
		Battery = FMath::Min(1.f, Battery + DeltaSeconds / FMath::Max(1.f, Settings->FlashlightRechargeSeconds));
	}
}

void AHCCharacter::UpdateFlashlightAim()
{
	if (!Flashlight->IsVisible())
	{
		return;
	}
	// Works for the local player (controller) and for remote players (replicated RemoteViewPitch).
	const FVector Aim = GetBaseAimRotation().Vector();
	Flashlight->SetWorldRotation(GetBaseAimRotation());
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(HCFlashlightBeam), false, this);
	// With a flashlight model the beam comes out of its lens. Against a wall it starts further back: never on the
	// other side (that would light up the next room) and never right at the surface.
	const UStaticMeshComponent* Held = FlashlightViewModel->IsVisible() ? FlashlightViewModel.Get()
		: FlashlightHandModel->IsVisible() ? FlashlightHandModel.Get() : nullptr;
	if (Held)
	{
		const FVector Lens = Held->GetComponentTransform().TransformPosition(FlashlightLensInModel);
		const FVector From = Held == FlashlightViewModel ? Camera->GetComponentLocation() : GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector Reach = Lens + Aim * HCFlashlightBeam::KeepAway;
		FVector Source = Lens;
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, Reach, ECC_Visibility, Query))
		{
			const float Room = FMath::Max(0.f, static_cast<float>(FVector::Dist(From, Hit.Location)) - HCFlashlightBeam::KeepAway);
			Source = From + (Reach - From).GetSafeNormal() * FMath::Min(Room, static_cast<float>(FVector::Dist(From, Lens)));
		}
		Flashlight->SetWorldLocation(Source);
	}
	// The eyes get used to a bright wall right in front: up close the beam gives no more light than at 1.5 m.
	// (With the game's fixed exposure the full beam would turn a near wall into a blinding white disc.)
	const FVector Start = Flashlight->GetComponentLocation();
	const float Distance = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Aim * HCFlashlightBeam::FullBrightFrom, ECC_Visibility, Query)
		? Hit.Distance : HCFlashlightBeam::FullBrightFrom;
	const float Wanted = HCFlashlightBeam::Candela * FMath::Clamp(FMath::Square(Distance / HCFlashlightBeam::FullBrightFrom), 0.04f, 1.f);
	const float Now = FMath::FInterpTo(Flashlight->Intensity, Wanted, GetWorld()->GetDeltaSeconds(), 10.f);
	if (!FMath::IsNearlyEqual(Now, Flashlight->Intensity, Flashlight->Intensity * 0.01f + 0.1f))
	{
		Flashlight->SetIntensity(Now);
	}
}

void AHCCharacter::OnRep_FlashlightOn()
{
	Flashlight->SetVisibility(bFlashlightOn);
	UpdateFlashlightLens();
	UpdateFlashlightAim();
}

void AHCCharacter::MulticastFlashlightClick_Implementation()
{
	if (!IsLocallyControlled())
	{
		HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::Click, GetActorLocation() + FVector(0.f, 0.f, 20.f), 0.5f, 900.f);
	}
}

void AHCCharacter::Server_SetBatteryFull()
{
	if (HasAuthority())
	{
		Battery = 1.f;
	}
}

void AHCCharacter::ServerSetFlashlight_Implementation(bool bOn)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFlashlightToggleTime < 0.15f)
	{
		return; // simple spam protection
	}
	LastFlashlightToggleTime = Now;
	// Both hands full with a container: no flashlight
	const bool bNewState = bOn && bAlive && Battery > 0.02f && !bCarrying;
	if (bNewState != bFlashlightOn)
	{
		bFlashlightOn = bNewState;
		OnRep_FlashlightOn();
		MulticastFlashlightClick();
	}
}

void AHCCharacter::ServerUnstuck_Implementation()
{
	if (TeleportToSafePoint(GetActorLocation()))
	{
		UE_LOG(LogHC, Log, TEXT("Unstuck: %s moved to %s"), *GetName(), *GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogHC, Warning, TEXT("Unstuck: no free safe point found for %s"), *GetName());
	}
}

bool AHCCharacter::TeleportToSafePoint(const FVector& Near)
{
	if (!HasAuthority())
	{
		return false;
	}
	TArray<FVector> Candidates;
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		Candidates.Append(It->GetSafePoints());
	}
	Candidates.Sort([&Near](const FVector& A, const FVector& B) { return FVector::DistSquared(A, Near) < FVector::DistSquared(B, Near); });
	for (const FVector& Point : Candidates)
	{
		// TeleportTo does an encroachment check and nudges out of geometry.
		if (TeleportTo(Point, GetActorRotation(), false, false))
		{
			GetCharacterMovement()->StopMovementImmediately();
			return true;
		}
	}
	return false;
}

void AHCCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	// Kill-Z protection (docs/04 §6.2): never end in the void, teleport back instead.
	if (HasAuthority())
	{
		UE_LOG(LogHC, Warning, TEXT("%s fell out of the world at %s, rescuing"), *GetName(), *GetActorLocation().ToCompactString());
		if (TeleportToSafePoint(FVector::ZeroVector))
		{
			return;
		}
	}
	if (!HasAuthority())
	{
		return; // server decides
	}
	Super::FellOutOfWorld(DmgType);
}

void AHCCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetController());
	if (!Input || !PC)
	{
		UE_LOG(LogHC, Error, TEXT("HCCharacter needs EnhancedInputComponent and HCPlayerController"));
		return;
	}
	const UHCInputConfig* Config = PC->GetInputConfig();

	Input->BindAction(Config->Move, ETriggerEvent::Triggered, this, &AHCCharacter::Input_Move);
	Input->BindAction(Config->Look, ETriggerEvent::Triggered, this, &AHCCharacter::Input_Look);
	Input->BindAction(Config->Sprint, ETriggerEvent::Started, this, &AHCCharacter::Input_SprintStarted);
	Input->BindAction(Config->Sprint, ETriggerEvent::Completed, this, &AHCCharacter::Input_SprintStopped);
	Input->BindAction(Config->Flashlight, ETriggerEvent::Started, this, &AHCCharacter::Input_ToggleFlashlight);
	Input->BindAction(Config->Interact, ETriggerEvent::Started, this, &AHCCharacter::Input_InteractStarted);
	Input->BindAction(Config->Interact, ETriggerEvent::Completed, this, &AHCCharacter::Input_InteractStopped);
	Input->BindAction(Config->Kill, ETriggerEvent::Started, this, &AHCCharacter::Input_Kill);
	Input->BindAction(Config->Shapeshift, ETriggerEvent::Started, this, &AHCCharacter::Input_Shapeshift);
	Input->BindAction(Config->Map, ETriggerEvent::Started, this, &AHCCharacter::Input_Map);
	Input->BindAction(Config->TrueForm, ETriggerEvent::Started, this, &AHCCharacter::Input_TrueForm);
	Input->BindAction(Config->Ability, ETriggerEvent::Started, this, &AHCCharacter::Input_Ability);
	Input->BindAction(Config->Emote, ETriggerEvent::Started, this, &AHCCharacter::Input_Emote);
}

void AHCCharacter::Input_Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (Controller && CanMove())
	{
		const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
	}
}

void AHCCharacter::Input_Look(const FInputActionValue& Value)
{
	const UHCGameInstance* Instance = GetGameInstance<UHCGameInstance>();
	const FVector2D Axis = Value.Get<FVector2D>() * (Instance ? Instance->GetMouseSensitivity() : 1.f);
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Instance && Instance->GetOption(TEXT("InvertMouseY"), 0.f) > 0.5f ? -Axis.Y : Axis.Y);
}

void AHCCharacter::Input_SprintStarted()
{
	if (UHCCharacterMovementComponent* Move = GetHCMovement())
	{
		Move->SetWantsToSprint(true);
	}
}

void AHCCharacter::Input_SprintStopped()
{
	if (UHCCharacterMovementComponent* Move = GetHCMovement())
	{
		Move->SetWantsToSprint(false);
	}
}

void AHCCharacter::Input_ToggleFlashlight()
{
	if (!bAlive)
	{
		return;
	}
	if (bCarrying)
	{
		if (AHCHUD* HUD = GetController<APlayerController>() ? GetController<APlayerController>()->GetHUD<AHCHUD>() : nullptr)
		{
			HUD->ShowCenterMessage(NSLOCTEXT("HC", "CarryNoLight", "Beide Hände sind voll."), 1.5f);
		}
		return;
	}
	// The switch clicks at once on this machine - also with an empty battery, when nothing lights up
	HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.6f);
	ServerSetFlashlight(!bFlashlightOn);
}

void AHCCharacter::Input_InteractStarted()
{
	Interaction->BeginInteract();
}

void AHCCharacter::Input_InteractStopped()
{
	Interaction->EndInteract();
}

void AHCCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCCharacter, bFlashlightOn);
	DOREPLIFETIME_CONDITION(AHCCharacter, Battery, COND_OwnerOnly);
	DOREPLIFETIME(AHCCharacter, bAlive);
	DOREPLIFETIME(AHCCharacter, bKillLocked);
	DOREPLIFETIME(AHCCharacter, bInVent);
	DOREPLIFETIME(AHCCharacter, bCarrying);
	DOREPLIFETIME(AHCCharacter, bUVLight);
	DOREPLIFETIME(AHCCharacter, bMonsterForm);
	DOREPLIFETIME(AHCCharacter, SpeedFactor);
	DOREPLIFETIME(AHCCharacter, BodyColor);
	DOREPLIFETIME(AHCCharacter, Appearance);
}

// ---------------------------------------------------------------------------
// Kill (docs/02 §7)

void AHCCharacter::ServerSetKillLocked(bool bLocked)
{
	if (!HasAuthority() || bKillLocked == bLocked)
	{
		return;
	}
	bKillLocked = bLocked;
	if (bLocked)
	{
		GetCharacterMovement()->StopMovementImmediately();
	}
}

void AHCCharacter::ServerSetInVent(bool bIn)
{
	if (!HasAuthority() || bInVent == bIn)
	{
		return;
	}
	bInVent = bIn;
	GetCharacterMovement()->StopMovementImmediately();
	// Gone for everybody: the hidden flag is replicated, and with it the figure, its name and its lights disappear
	SetActorHiddenInGame(bIn);
	if (bIn && bFlashlightOn)
	{
		// No beam out of the floor
		bFlashlightOn = false;
		OnRep_FlashlightOn();
	}
}

void AHCCharacter::Input_Kill()
{
	// Does nothing visible or audible unless this player is the Wandler and has a target (docs/01 §4.2)
	KillComponent->TryKill();
}

void AHCCharacter::MulticastPlayKillAttack_Implementation(FVector_NetQuantize VictimLocation, AHCCharacter* Victim)
{
	// Witnesses see the figure in its current shape lunge, and hear a scream up to about 20 m away
	KillAttackTime = 0.f;
	if (bMonsterForm && bRealModel && Models && Models->MonsterAttack && GetMesh()->GetSkeletalMeshAsset() == Models->MonsterMesh)
	{
		// In its true form the monster really strikes
		MonsterAnim = Models->MonsterAttack;
		MonsterAnimLockedUntil = GetWorld()->GetTimeSeconds() + Models->MonsterAttack->GetPlayLength();
		GetMesh()->PlayAnimation(Models->MonsterAttack, false);
	}
	// Very loud on purpose: everybody hears it up to about 30 m away. The victim hears it from its jumpscare
	// (AHCJumpscareDirector), at full force and in step with the scare ("lights out" holds it back until the slam).
	if (!Victim || !Victim->IsLocallyControlled())
	{
		HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::Scream, VictimLocation, 2.f, 3000.f);
	}
}

void AHCCharacter::SetNameTagHiddenLocally(bool bHide)
{
	NameTag->SetHiddenInGame(bHide);
}

void AHCCharacter::UpdateKillAttack(float DeltaSeconds)
{
	if (KillAttackTime < 0.f)
	{
		return;
	}
	KillAttackTime += DeltaSeconds;
	constexpr float AttackSeconds = 0.5f;
	const float Alpha = FMath::Clamp(KillAttackTime / AttackSeconds, 0.f, 1.f);
	// Placeholder for the attack animation: the body snaps forward and back, in place (docs/02 §7.7)
	const float Lunge = FMath::Sin(Alpha * PI) * 55.f;
	BodyMesh->SetRelativeLocation(FVector(Lunge, 0.f, -15.f));
	HeadMesh->SetRelativeLocation(FVector(Lunge * 1.3f, 0.f, 68.f));
	VisorMesh->SetRelativeLocation(FVector(16.f + Lunge * 1.3f, 0.f, 70.f));
	if (Alpha >= 1.f)
	{
		KillAttackTime = -1.f;
	}
}

void AHCCharacter::UpdateMonsterMarker()
{
	// Only a ghost's own PC knows the Wandler (sent once after death) and draws the marker - nothing is replicated
	bool bShow = false;
	const AHCPlayerController* LocalPC = Cast<AHCPlayerController>(GetWorld()->GetFirstPlayerController());
	const AHCPlayerState* LocalPS = LocalPC ? LocalPC->GetPlayerState<AHCPlayerState>() : nullptr;
	const APlayerState* MyPS = GetPlayerState();
	if (LocalPC && LocalPS && MyPS && !LocalPS->IsAlive())
	{
		bShow = LocalPC->IsMonsterRevealed(MyPS->GetPlayerId());
	}
	else if (LocalPS && MyPS && LocalPS->IsAlive() && LocalPS->GetRoleInfo().Role == EHCRole::Monster && LocalPS->GetRoleInfo().PartnerId == MyPS->GetPlayerId())
	{
		// Two Wandlers know each other: the partner wears the marker in the own view, in every shape
		bShow = !IsHidden();
	}
	if (MonsterMarker->IsVisible() != bShow)
	{
		MonsterMarker->SetVisibility(bShow);
	}
}

bool AHCCharacter::IsMonsterMarkerVisible() const
{
	return MonsterMarker && MonsterMarker->IsVisible();
}
