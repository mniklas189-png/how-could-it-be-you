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
	if (UMaterialInstanceDynamic* PanelMat = HCGreybox::MakeColored(this, FLinearColor(0.25f, 0.27f, 0.3f)))
	{
		Panel->SetMaterial(0, PanelMat);
	}

	OpenAlpha = DoorState == EHCDoorState::Open ? 1.f : 0.f;
	UpdatePanel();

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
	if (!HasAuthority() || DoorState == EHCDoorState::Locked)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
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
	if (StatusMaterial)
	{
		const FLinearColor Color = DoorState == EHCDoorState::Open ? FLinearColor(0.1f, 1.f, 0.2f)
			: DoorState == EHCDoorState::Locked ? FLinearColor(1.f, 0.3f, 0.f) : FLinearColor::Red;
		StatusMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
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
	// Panel center: closed = middle of the opening, open = fully inside the ceiling/lintel area above.
	const float Lift = OpenAlpha * (OpeningHeight - 5.f);
	Panel->SetRelativeLocation(FVector(0.f, 0.f, OpeningHeight * 0.5f + Lift));
	// Only block pawns when (almost) fully closed, so a closing door never traps anyone.
	const bool bBlocking = OpenAlpha < 0.02f && DoorState != EHCDoorState::Open;
	Panel->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly);
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
