#include "Sabotage/HCDoor.h"

#include "Characters/HCCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/HCGreyboxAssets.h"
#include "Audio/HCProceduralAudio.h"
#include "Misc/CommandLine.h"
#include "World/HCModelAssets.h"
#include "Engine/StaticMesh.h"

AHCDoor::AHCDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetDormancy(DORM_DormantAll);
	SetNetUpdateFrequency(10.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
	Panel->SetStaticMesh(HCGreybox::Cube());
	Panel->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// Only a locked door is something to interact with (emergency release)
	Panel->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Ignore);
	Panel->SetCanEverAffectNavigation(false);

	StatusLightA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StatusLightA"));
	StatusLightA->SetupAttachment(Root);
	StatusLightA->SetStaticMesh(HCGreybox::Cube());
	StatusLightA->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StatusLightA->SetCanEverAffectNavigation(false);

	StatusLightB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StatusLightB"));
	StatusLightB->SetupAttachment(Root);
	StatusLightB->SetStaticMesh(HCGreybox::Cube());
	StatusLightB->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StatusLightB->SetCanEverAffectNavigation(false);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
}

void AHCDoor::Configure(float InWidth, float InHeight, float InWallThickness, bool bInSabotageable, FName InDoorId)
{
	OpeningWidth = InWidth;
	OpeningHeight = InHeight;
	WallThickness = InWallThickness;
	bSabotageable = bInSabotageable;
	DoorId = InDoorId;
	ApplyGeometry();
}

void AHCDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyGeometry();
}

void AHCDoor::ApplyGeometry()
{
	// Local space: door plane = YZ, X = through the door. Origin = bottom center of the opening.
	Panel->SetRelativeScale3D(FVector(WallThickness * 0.5f / 100.f, OpeningWidth / 100.f, OpeningHeight / 100.f));
	// Status lights left/right of the opening on both sides, at 200 cm
	const float SideY = OpeningWidth * 0.5f - 15.f;
	StatusLightA->SetRelativeScale3D(FVector(0.06f, 0.1f, 0.1f));
	StatusLightA->SetRelativeLocation(FVector(WallThickness * 0.5f + 3.f, SideY, OpeningHeight + 20.f));
	StatusLightB->SetRelativeScale3D(FVector(0.06f, 0.1f, 0.1f));
	StatusLightB->SetRelativeLocation(FVector(-WallThickness * 0.5f - 3.f, -SideY, OpeningHeight + 20.f));
	// 250 cm into each room, full width, full height
	Trigger->SetBoxExtent(FVector(250.f, OpeningWidth * 0.5f, OpeningHeight * 0.5f));
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, OpeningHeight * 0.5f));
	UpdatePanel();
}

void AHCDoor::BeginPlay()
{
	Super::BeginPlay();

	StatusMaterial = HCGreybox::MakeColored(this, FLinearColor::Red);
	if (StatusMaterial)
	{
		StatusLightA->SetMaterial(0, StatusMaterial);
		StatusLightB->SetMaterial(0, StatusMaterial);
	}
	PanelMaterial = HCGreybox::MakeColored(this, FLinearColor(0.25f, 0.27f, 0.3f));
	if (PanelMaterial)
	{
		Panel->SetMaterial(0, PanelMaterial);
	}
	// What you see are two halves that part in the middle and slide into the wall on either side: real door leaves
	// from the model packs (thin along their X), or grey slabs without them. The panel stays as unseen collision.
	const UHCModelSet* Models = HCModels::Get();
	UStaticMesh* Leaf = Models ? Models->FindProp(TEXT("DoorLeaf")) : nullptr;
	UStaticMesh* Glass = Models ? Models->FindProp(TEXT("DoorGlass")) : nullptr;
	const FBox Shape = Leaf ? Leaf->GetBoundingBox() : FBox(FVector(-50.0), FVector(50.0));
	const FVector Size = Shape.GetSize().ComponentMax(FVector(1.0));
	// A little thinner than the wall, so an open half disappears inside it
	const FVector LeafScale(WallThickness * 0.9 / Size.X, OpeningWidth * 0.5 / Size.Y, OpeningHeight / Size.Z);
	const FVector Center = Shape.GetCenter() * LeafScale;
	for (const float Side : { -1.f, 1.f })
	{
		for (UStaticMesh* PartMesh : { Leaf ? Leaf : HCGreybox::Cube(), Leaf ? Glass : nullptr })
		{
			if (!PartMesh)
			{
				continue;
			}
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetupAttachment(Root);
			Part->SetStaticMesh(PartMesh);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			Part->ComponentTags.Add(HCModels::DressTag); // the outline of a locked door
			if (!Leaf && PanelMaterial)
			{
				Part->SetMaterial(0, PanelMaterial);
			}
			Part->RegisterComponent();
			Part->SetRelativeScale3D(LeafScale);
			LeafParts.Add(Part);
			LeafHomes.Add(FVector(-Center.X, Side * OpeningWidth * 0.25 - Center.Y, OpeningHeight * 0.5 - Center.Z));
			LeafSides.Add(Side);
		}
	}
	Panel->SetVisibility(false);
	OnRep_DoorState();

	OpenAlpha = DoorState == EHCDoorState::Open ? 1.f : 0.f;
	UpdatePanel();
	HeardState = DoorState;
	bSoundReady = true;
#if !UE_BUILD_SHIPPING
	// Development: -HCDoorsAjar freezes every door half open (for pictures)
	if (FParse::Param(FCommandLine::Get(), TEXT("HCDoorsAjar")))
	{
		OpenAlpha = 0.5f;
		UpdatePanel();
		SetActorTickEnabled(false);
	}
	// Development: -HCDoorsOpenAt=<seconds> opens every door once as if somebody walked up to it
	float OpenAt = 0.f;
	if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("HCDoorsOpenAt="), OpenAt))
	{
		FTimerHandle OpenHandle;
		GetWorldTimerManager().SetTimer(OpenHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			LastOccupiedTime = GetWorld()->GetTimeSeconds();
			SetDoorState(EHCDoorState::Open);
		}), OpenAt, false);
	}
#endif

	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AHCDoor::OnTriggerBegin);
		Trigger->OnComponentEndOverlap.AddDynamic(this, &AHCDoor::OnTriggerEnd);
		// Safety net (docs/02 §11.3): re-check every 0.25 s
		GetWorldTimerManager().SetTimer(EvaluateTimer, this, &AHCDoor::ServerEvaluate, 0.25f, true, 0.25f);
	}
}

void AHCDoor::OnTriggerBegin(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (Cast<AHCCharacter>(OtherActor))
	{
		ServerEvaluate();
	}
}

void AHCDoor::OnTriggerEnd(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32)
{
	if (Cast<AHCCharacter>(OtherActor))
	{
		LastOccupiedTime = GetWorld()->GetTimeSeconds();
	}
}

bool AHCDoor::IsAnyLivingPlayerInTrigger() const
{
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, AHCCharacter::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		const AHCCharacter* Character = Cast<AHCCharacter>(Actor);
		if (Character && Character->IsAlive())
		{
			return true;
		}
	}
	return false;
}

void AHCDoor::ServerEvaluate()
{
	if (!HasAuthority())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (DoorState == EHCDoorState::Locked)
	{
		// Opens by itself when the time is over - and at once if someone ended up inside the frame
		if (Now >= LockEndTime || IsFrameOccupied())
		{
			ServerUnlock();
		}
		return;
	}
	if (bLockPending)
	{
		if (!IsFrameOccupied())
		{
			bLockPending = false;
			LockEndTime = Now + LockSeconds;
			SetDoorState(EHCDoorState::Locked);
			return;
		}
		if (Now - LockRequestedAt >= 2.f)
		{
			bLockPending = false; // never closes on a player: the sabotage fails (docs/01 §7 rule 3)
		}
	}
	if (IsAnyLivingPlayerInTrigger())
	{
		LastOccupiedTime = Now;
		SetDoorState(EHCDoorState::Open);
	}
	else if (DoorState == EHCDoorState::Open && Now - LastOccupiedTime >= UHCGameSettings::Get()->DoorCloseDelay)
	{
		SetDoorState(EHCDoorState::Closed);
	}
}

void AHCDoor::SetDoorState(EHCDoorState NewState)
{
	if (DoorState == NewState)
	{
		return;
	}
	FlushNetDormancy();
	DoorState = NewState;
	OnRep_DoorState(); // listen server: run the rep notify locally too
}

void AHCDoor::OnRep_DoorState()
{
	const bool bLocked = DoorState == EHCDoorState::Locked;
	if (PanelMaterial)
	{
		PanelMaterial->SetVectorParameterValue(TEXT("Color"), bLocked ? FLinearColor(0.55f, 0.07f, 0.04f) : FLinearColor(0.25f, 0.27f, 0.3f));
	}
	Panel->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, bLocked ? ECR_Block : ECR_Ignore);
	if (StatusMaterial)
	{
		const FLinearColor Color = DoorState == EHCDoorState::Open ? FLinearColor(0.1f, 1.f, 0.2f)
			: DoorState == EHCDoorState::Locked ? FLinearColor(1.f, 0.3f, 0.f) : FLinearColor::Red;
		StatusMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	// Everybody nearby hears a door open or close
	if (bSoundReady && DoorState != HeardState && (DoorState == EHCDoorState::Open || HeardState == EHCDoorState::Open))
	{
		HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::Door, GetActorLocation() + FVector(0.f, 0.f, 120.f), 0.65f, 1500.f);
	}
	HeardState = DoorState;
}

void AHCDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = DoorState == EHCDoorState::Open ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(OpenAlpha, Target))
	{
		const float Speed = 1.f / FMath::Max(0.05f, UHCGameSettings::Get()->DoorOpenSeconds);
		OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, Target, DeltaSeconds, Speed);
		UpdatePanel();
	}
}

void AHCDoor::UpdatePanel()
{
	if (!Panel)
	{
		return;
	}
	// The two halves part in the middle and slide into the wall on either side
	const float Slide = OpenAlpha * (OpeningWidth * 0.5f - 3.f);
	for (int32 Index = 0; Index < LeafParts.Num(); ++Index)
	{
		if (UStaticMeshComponent* Part = LeafParts[Index])
		{
			Part->SetRelativeLocation(LeafHomes[Index] + FVector(0.f, LeafSides[Index] * Slide, 0.f));
		}
	}
	// The unseen panel fills the opening. It only blocks pawns when (almost) fully closed, so a closing door never
	// traps anyone; it stops sight and reach until the gap is half open, then it is out of the way.
	Panel->SetRelativeLocation(FVector(0.f, 0.f, OpeningHeight * 0.5f));
	const bool bBlocking = OpenAlpha < 0.02f && DoorState != EHCDoorState::Open;
	Panel->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : OpenAlpha < 0.5f ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	Panel->SetCollisionResponseToChannel(ECC_Pawn, bBlocking ? ECR_Block : ECR_Ignore);
}

void AHCDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCDoor, DoorState);
	DOREPLIFETIME(AHCDoor, bSabotageable);
	DOREPLIFETIME(AHCDoor, DoorId);
	DOREPLIFETIME(AHCDoor, OpeningWidth);
	DOREPLIFETIME(AHCDoor, OpeningHeight);
	DOREPLIFETIME(AHCDoor, WallThickness);
}

// ---------------------------------------------------------------------------
// Sabotage lock (docs/02 §11.3)

bool AHCDoor::IsFrameOccupied() const
{
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, AHCCharacter::StaticClass());
	for (const AActor* Actor : Overlapping)
	{
		const AHCCharacter* Character = Cast<AHCCharacter>(Actor);
		// Local X runs through the door: the frame is the wall plus one capsule radius on each side
		if (Character && Character->IsAlive() && FMath::Abs(GetActorTransform().InverseTransformPosition(Character->GetActorLocation()).X) < WallThickness * 0.5f + 60.f)
		{
			return true;
		}
	}
	return false;
}

void AHCDoor::ServerRequestLock(float Seconds)
{
	if (HasAuthority() && DoorState != EHCDoorState::Locked)
	{
		bLockPending = true;
		LockSeconds = Seconds;
		LockRequestedAt = GetWorld()->GetTimeSeconds();
		ServerEvaluate();
	}
}

void AHCDoor::ServerUnlock()
{
	bLockPending = false;
	if (HasAuthority() && DoorState == EHCDoorState::Locked)
	{
		SetDoorState(EHCDoorState::Closed);
		ServerEvaluate(); // opens right away if someone is waiting in front of it
	}
}

FText AHCDoor::GetDisplayName() const
{
	return FText::FromString(DoorId.ToString().Replace(TEXT("_"), TEXT(" ")));
}

bool AHCDoor::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	return Who && Who->CanAct() && DoorState == EHCDoorState::Locked;
}

FText AHCDoor::GetInteractionText(const AHCCharacter* Who) const
{
	return NSLOCTEXT("HC", "DoorRelease", "Notentriegelung (halten)");
}

float AHCDoor::GetHoldDuration(const AHCCharacter* Who) const
{
	return 3.f;
}

void AHCDoor::ExecuteInteraction(AHCCharacter* Who)
{
	UE_LOG(LogHC, Log, TEXT("Door %s released by hand"), *DoorId.ToString());
	ServerUnlock();
}

UPrimitiveComponent* AHCDoor::GetHighlightComponent() const
{
	return Panel;
}
