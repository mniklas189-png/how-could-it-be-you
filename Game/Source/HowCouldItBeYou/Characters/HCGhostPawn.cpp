#include "Characters/HCGhostPawn.h"

#include "Core/HCKeyBindings.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCInputConfig.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "InputActionValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "UObject/SoftObjectPath.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCStationBuilder.h"

namespace
{
	constexpr float GhostReach = 200.f;
	constexpr float GhostReportInterval = 1.f / 15.f;
}

AHCGhostPawn::AHCGhostPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;

	// No collision with the world or with players; the map bounds are enforced by clamping
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(30.f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetCanEverAffectNavigation(false);
	RootComponent = Sphere;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Sphere);
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(90.f);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetStaticMesh(HCGreybox::Sphere());
	Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.65f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->SetOwnerNoSee(true);
	Mesh->SetVisibility(false); // switched on per machine, only for ghosts
	// Translucent engine material; set here so that it is a hard reference and gets packaged
	if (UMaterialInterface* Translucent = Cast<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/M_SimpleTranslucent.M_SimpleTranslucent")).TryLoad()))
	{
		Mesh->SetMaterial(0, Translucent);
	}

	OwnLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("OwnLight"));
	OwnLight->SetupAttachment(Sphere);
	OwnLight->SetIntensityUnits(ELightUnits::Candelas);
	OwnLight->SetIntensity(6.f);
	OwnLight->SetAttenuationRadius(900.f);
	OwnLight->SetLightColor(FLinearColor(0.6f, 0.75f, 1.f));
	OwnLight->SetCastShadows(false);
	OwnLight->SetVisibility(false);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 450.f;
	Movement->Acceleration = 3000.f;
	Movement->Deceleration = 4000.f;
}

void AHCGhostPawn::BeginPlay()
{
	Super::BeginPlay();
	Movement->MaxSpeed = UHCGameSettings::Get()->GhostFlySpeed;
	OnRep_Identity();
}

void AHCGhostPawn::SetIdentity(int32 InIdentity)
{
	Identity = InIdentity;
	OnRep_Identity();
}

void AHCGhostPawn::OnRep_Identity()
{
	if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(0))
	{
		FLinearColor Color = HCIdentity::Color(Identity);
		Color.A = 0.35f;
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

FBox AHCGhostPawn::GetBounds() const
{
	// The first one only. An if, not a for-loop that always leaves: clang (Linux) rejects such loops.
	if (TActorIterator<AHCStationBuilder> It(GetWorld()); It)
	{
		return It->GetGhostBounds();
	}
	return FBox(FVector(-100000.f), FVector(100000.f));
}

bool AHCGhostPawn::IsMeshVisibleLocally() const
{
	return Mesh->IsVisible();
}

void AHCGhostPawn::UpdateLocalVisibility()
{
	// Each machine decides for itself: ghosts are only drawn if the local player is a ghost too.
	// This is what hides ghosts from a living host, who has every actor (docs/02 §7.10).
	const APlayerController* LocalPC = GetWorld()->GetFirstPlayerController();
	const AHCPlayerState* LocalPS = LocalPC ? LocalPC->GetPlayerState<AHCPlayerState>() : nullptr;
	const bool bLocalIsGhost = LocalPS && !LocalPS->IsAlive();
	const bool bMine = IsLocallyControlled();
	Mesh->SetVisibility(bLocalIsGhost && !bMine);
	OwnLight->SetVisibility(bMine);
}

void AHCGhostPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();

	if (Now >= NextVisibilityTime)
	{
		NextVisibilityTime = Now + 0.3f;
		UpdateLocalVisibility();
	}

	if (IsLocallyControlled())
	{
		// Stay inside the map bounds
		const FBox Bounds = GetBounds();
		const FVector Location = GetActorLocation();
		const FVector Clamped = Bounds.GetClosestPointTo(Location);
		if (!Clamped.Equals(Location, 0.5))
		{
			SetActorLocation(Clamped);
			Movement->Velocity = FVector::ZeroVector;
		}
		if (!HasAuthority() && Now >= NextReportTime)
		{
			NextReportTime = Now + GhostReportInterval;
			ServerReportLocation(GetActorLocation());
		}
		if (Now >= NextFocusTime && !bLocalHolding)
		{
			NextFocusTime = Now + 0.2f;
			UpdateFocus();
		}
	}

	if (HasAuthority() && ServerHoldStation.IsValid())
	{
		AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
		const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
		const bool bStillValid = PS && GS && GS->GetMatchPhase() == EHCMatchPhase::Playing && IsStationInReach(ServerHoldStation.Get(), 80.f);
		if (!bStillValid)
		{
			ServerHoldStation.Reset();
			ClientTaskHoldEnded();
		}
		else if (Now >= ServerHoldEnd)
		{
			AHCTaskStation* Station = ServerHoldStation.Get();
			ServerHoldStation.Reset();
			PS->GetTaskComponent()->ServerCompleteHoldStep(Station);
			ClientTaskHoldEnded();
		}
	}
}

// ---------------------------------------------------------------------------
// Movement

void AHCGhostPawn::ServerReportLocation_Implementation(FVector_NetQuantize NewLocation)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const double Delta = FMath::Clamp(Now - LastReportTime, 0.0, 1.0);
	LastReportTime = Now;
	// Not faster than the fly speed allows (with a generous tolerance for jitter), and never outside the bounds
	const float MaxStep = Movement->MaxSpeed * 1.5f * static_cast<float>(Delta) + 150.f;
	FVector Target = NewLocation;
	const FVector Current = GetActorLocation();
	if (FVector::Dist(Current, Target) > MaxStep)
	{
		Target = Current + (Target - Current).GetSafeNormal() * MaxStep;
	}
	SetActorLocation(GetBounds().GetClosestPointTo(Target));
}

void AHCGhostPawn::ServerTeleport(const FVector& Location)
{
	SetActorLocation(GetBounds().GetClosestPointTo(Location));
	ForceNetUpdate();
	ClientTeleport(GetActorLocation());
}

void AHCGhostPawn::ClientTeleport_Implementation(FVector_NetQuantize Location)
{
	SetActorLocation(Location);
	NextFocusTime = 0.f;
}

void AHCGhostPawn::FellOutOfWorld(const UDamageType& DmgType)
{
	// Unreal would simply destroy a ghost below Kill-Z; put it back into the station instead (docs/04 §6)
	if (HasAuthority())
	{
		SetActorLocation(GetBounds().GetCenter());
	}
}

bool AHCGhostPawn::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// Only the owner and other ghosts ever receive a ghost; living clients do not get it at all
	const APlayerController* ViewerPC = Cast<APlayerController>(RealViewer);
	if (ViewerPC && ViewerPC == GetController())
	{
		return true;
	}
	const AHCPlayerState* ViewerPS = ViewerPC ? ViewerPC->GetPlayerState<AHCPlayerState>() : nullptr;
	return ViewerPS && !ViewerPS->IsAlive();
}

// ---------------------------------------------------------------------------
// Tasks: ghosts finish their own remaining tasks (docs/02 §9.5)

bool AHCGhostPawn::IsStationInReach(const AHCTaskStation* Station, float Extra) const
{
	return Station && Station->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(GetActorLocation()) <= FMath::Square(GhostReach + Extra);
}

void AHCGhostPawn::UpdateFocus()
{
	FocusedStation.Reset();
	FocusWaitSeconds = 0.f;
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	const UHCTaskComponent* Tasks = PS ? PS->GetTaskComponent() : nullptr;
	if (!Tasks || !GS || GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		return;
	}
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AHCTaskStation> It(GetWorld()); It; ++It)
	{
		float Wait = 0.f;
		if (Tasks->FindPendingIndexAt(*It, &Wait) == INDEX_NONE || !IsStationInReach(*It, 0.f))
		{
			continue;
		}
		const float Distance = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			FocusedStation = *It;
			FocusWaitSeconds = Wait;
		}
	}
}

FText AHCGhostPawn::GetPromptText() const
{
	const AHCTaskStation* Station = FocusedStation.Get();
	if (!Station)
	{
		return FText::GetEmpty();
	}
	if (FocusWaitSeconds > 0.f)
	{
		return FText::Format(NSLOCTEXT("HC", "TaskWait", "Noch nicht bereit ({0} s)"), FMath::CeilToInt(FocusWaitSeconds));
	}
	return FText::Format(NSLOCTEXT("HC", "PromptKeyFmt", "[{0}] {1}"), HCKeys::ShortName(EHCBinding::Interact), Station->GetInteractionText(nullptr));
}

float AHCGhostPawn::GetHoldProgress() const
{
	if (!bLocalHolding || LocalHoldDuration <= 0.f)
	{
		return -1.f;
	}
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - LocalHoldStart) / LocalHoldDuration, 0.f, 1.f);
}

void AHCGhostPawn::BeginTaskInteract()
{
	AHCTaskStation* Station = FocusedStation.Get();
	if (!Station || FocusWaitSeconds > 0.f)
	{
		return;
	}
	const float Hold = Station->GetHoldDuration(nullptr);
	if (Hold > 0.f)
	{
		bLocalHolding = true;
		LocalHoldStart = GetWorld()->GetTimeSeconds();
		LocalHoldDuration = Hold;
	}
	ServerBeginTask(Station);
}

void AHCGhostPawn::EndTaskInteract()
{
	if (bLocalHolding)
	{
		bLocalHolding = false;
		ServerEndTask();
	}
}

void AHCGhostPawn::ServerBeginTask_Implementation(AHCTaskStation* Station)
{
	AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	UHCTaskComponent* Tasks = PS ? PS->GetTaskComponent() : nullptr;
	float Wait = 0.f;
	if (!Station || !Tasks || !GS || GS->GetMatchPhase() != EHCMatchPhase::Playing
		|| Tasks->FindPendingIndexAt(Station, &Wait) == INDEX_NONE || Wait > 0.f || !IsStationInReach(Station, 80.f))
	{
		ClientTaskHoldEnded();
		return;
	}
	const float Hold = Station->GetHoldDuration(nullptr);
	if (Hold > 0.f)
	{
		ServerHoldStation = Station;
		ServerHoldEnd = GetWorld()->GetTimeSeconds() + Hold;
	}
	else if (Tasks->ServerBeginTimedStep(Station))
	{
		// Same overlay as for the living; nothing at the station changes for anyone else
		if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetController()))
		{
			PC->ClientOpenTaskOverlay(Station);
		}
	}
}

void AHCGhostPawn::ServerEndTask_Implementation()
{
	ServerHoldStation.Reset();
}

void AHCGhostPawn::ClientTaskHoldEnded_Implementation()
{
	bLocalHolding = false;
	NextFocusTime = 0.f;
}

// ---------------------------------------------------------------------------
// Input

void AHCGhostPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetController());
	if (!Input || !PC)
	{
		return;
	}
	const UHCInputConfig* Config = PC->GetInputConfig();
	Input->BindAction(Config->Move, ETriggerEvent::Triggered, this, &AHCGhostPawn::Input_Move);
	Input->BindAction(Config->Look, ETriggerEvent::Triggered, this, &AHCGhostPawn::Input_Look);
	Input->BindAction(Config->GhostUp, ETriggerEvent::Triggered, this, &AHCGhostPawn::Input_Up);
	Input->BindAction(Config->GhostDown, ETriggerEvent::Triggered, this, &AHCGhostPawn::Input_Down);
	Input->BindAction(Config->Interact, ETriggerEvent::Started, this, &AHCGhostPawn::BeginTaskInteract);
	Input->BindAction(Config->Interact, ETriggerEvent::Completed, this, &AHCGhostPawn::EndTaskInteract);
}

void AHCGhostPawn::Input_Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (Controller)
	{
		// Fly where you look
		const FRotator Rotation = Controller->GetControlRotation();
		AddMovementInput(FRotationMatrix(Rotation).GetUnitAxis(EAxis::X), Axis.Y);
		AddMovementInput(FRotationMatrix(FRotator(0.f, Rotation.Yaw, 0.f)).GetUnitAxis(EAxis::Y), Axis.X);
	}
}

void AHCGhostPawn::Input_Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void AHCGhostPawn::Input_Up(const FInputActionValue& Value)
{
	AddMovementInput(FVector::UpVector, 1.f);
}

void AHCGhostPawn::Input_Down(const FInputActionValue& Value)
{
	AddMovementInput(FVector::UpVector, -1.f);
}

void AHCGhostPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCGhostPawn, Identity);
}
