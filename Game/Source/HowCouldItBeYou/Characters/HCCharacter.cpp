#include "Characters/HCCharacter.h"
#include "Core/HCGameInstance.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "Characters/HCUneaseComponent.h"
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

AHCCharacter::AHCCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UHCCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
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
	Flashlight->SetIntensity(900.f);
	Flashlight->SetAttenuationRadius(1500.f);
	Flashlight->SetInnerConeAngle(10.f);
	Flashlight->SetOuterConeAngle(24.f);
	Flashlight->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f));
	Flashlight->SetCastShadows(true);
	Flashlight->SetUsingAbsoluteRotation(true);
	Flashlight->SetVisibility(false);

	HelmetLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HelmetLight"));
	HelmetLight->SetupAttachment(GetCapsuleComponent());
	HelmetLight->SetRelativeLocation(FVector(25.f, 0.f, 80.f));
	HelmetLight->SetIntensityUnits(ELightUnits::Candelas);
	HelmetLight->SetIntensity(2.f);
	HelmetLight->SetAttenuationRadius(300.f);
	HelmetLight->SetLightColor(FLinearColor(0.7f, 0.85f, 1.f));
	HelmetLight->SetCastShadows(false);

	Interaction = CreateDefaultSubobject<UHCInteractionComponent>(TEXT("Interaction"));
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
	OnRep_FlashlightOn();
	OnRep_Appearance();
}

bool AHCCharacter::CanAct() const
{
	if (!bAlive || bKillLocked || ShapeshiftComponent->IsChanging())
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
	if (bKillLocked || ShapeshiftComponent->IsChanging())
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
	for (UMaterialInstanceDynamic* Tinted : TintMaterials)
	{
		if (Tinted && Models)
		{
			Tinted->SetVectorParameterValue(Models->TintParameter, BodyColor);
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
	UpdateFlashlightAim();
	UpdateKillAttack(DeltaSeconds);
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
	Flashlight->SetWorldRotation(GetBaseAimRotation());
}

void AHCCharacter::OnRep_FlashlightOn()
{
	Flashlight->SetVisibility(bFlashlightOn);
	UpdateFlashlightAim();
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
	const bool bNewState = bOn && bAlive && Battery > 0.02f;
	if (bNewState != bFlashlightOn)
	{
		bFlashlightOn = bNewState;
		OnRep_FlashlightOn();
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
	AddControllerPitchInput(Axis.Y);
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

void AHCCharacter::Input_Kill()
{
	// Does nothing visible or audible unless this player is the Wandler and has a target (docs/01 §4.2)
	KillComponent->TryKill();
}

void AHCCharacter::MulticastPlayKillAttack_Implementation(FVector_NetQuantize VictimLocation)
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
	// Very loud on purpose: the victim hears it at full force, everybody else up to about 30 m away
	HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::Scream, VictimLocation, 2.f, 3000.f);
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
	if (MonsterMarker->IsVisible() != bShow)
	{
		MonsterMarker->SetVisibility(bShow);
	}
}

bool AHCCharacter::IsMonsterMarkerVisible() const
{
	return MonsterMarker && MonsterMarker->IsVisible();
}
