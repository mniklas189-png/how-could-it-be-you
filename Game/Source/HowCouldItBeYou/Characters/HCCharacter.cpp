#include "Characters/HCCharacter.h"

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
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
	MonsterMarker->SetRelativeLocation(FVector(0.f, 0.f, 128.f));
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
	}
	if (UMaterialInstanceDynamic* MarkerMaterial = HCGreybox::MakeColored(this, FLinearColor(1.f, 0.02f, 0.02f)))
	{
		MonsterMarker->SetMaterial(0, MarkerMaterial);
	}
	OnRep_FlashlightOn();
}

bool AHCCharacter::CanAct() const
{
	if (!bAlive || bKillLocked)
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
	if (bKillLocked)
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
}

void AHCCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
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
	const FVector2D Axis = Value.Get<FVector2D>();
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
	DOREPLIFETIME(AHCCharacter, BodyColor);
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
	HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::Scream, VictimLocation, 1.f, 2000.f);
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
