#include "Abilities/HCRemains.h"

#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameSettings.h"
#include "Core/HCTypes.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "World/HCGreyboxAssets.h"

AHCRemains::AHCRemains()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetDormancy(DORM_DormantAll);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	auto MakePart = [this, Root](const TCHAR* PartName, UStaticMesh* PartMesh, const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(Root);
		Part->SetStaticMesh(PartMesh);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		// No collision with anything: remains never block a way (docs/01 §4.2)
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		return Part;
	};
	// Actor origin = point on the floor
	Pool = MakePart(TEXT("Pool"), HCGreybox::Cylinder(), FVector(0.f, 0.f, 0.6f), FVector(1.5f, 1.3f, 0.01f));
	Tag = MakePart(TEXT("Tag"), HCGreybox::Cube(), FVector(0.f, 0.f, 2.5f), FVector(0.14f, 0.09f, 0.02f));

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(Root);
	NameLabel->SetRelativeLocation(FVector(-22.f, 0.f, 3.f));
	NameLabel->SetRelativeRotation(FRotator(90.f, 180.f, 0.f)); // lying flat on the floor
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetVerticalAlignment(EVRTA_TextCenter);
	NameLabel->SetWorldSize(14.f);
	NameLabel->SetTextRenderColor(FColor(230, 230, 230));
	NameLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AHCRemains::Configure(const FString& InVictimName, int32 InVictimIdentity)
{
	VictimName = InVictimName;
	VictimIdentity = InVictimIdentity;
}

void AHCRemains::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInstanceDynamic* PoolMaterial = HCGreybox::MakeColored(this, FLinearColor(0.25f, 0.005f, 0.005f)))
	{
		Pool->SetMaterial(0, PoolMaterial);
	}
	OnRep_Victim();
}

void AHCRemains::OnRep_Victim()
{
	// Victims are never disguised, so this is always the real name and identity (docs/02 §7.9)
	NameLabel->SetText(FText::FromString(VictimName));
	if (UMaterialInstanceDynamic* TagMaterial = HCGreybox::MakeColored(this, HCIdentity::Color(VictimIdentity)))
	{
		Tag->SetMaterial(0, TagMaterial);
	}
}

void AHCRemains::ServerMarkReported()
{
	FlushNetDormancy();
	bReported = true;
}

bool AHCRemains::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	if (!Who || !Who->CanAct())
	{
		return false;
	}
	if (bReported)
	{
		OutReason = NSLOCTEXT("HC", "AlreadyReported", "Bereits gemeldet");
		return false;
	}
	// Up to 250 cm around the remains; the server allows the ping tolerance on top (docs/02 §7.11)
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const float MaxDistance = Settings->ReportRange + (HasAuthority() ? Settings->ServerRangeTolerance : 0.f);
	if (FVector::Dist2D(Who->GetActorLocation(), GetActorLocation()) > MaxDistance || FMath::Abs(Who->GetActorLocation().Z - GetActorLocation().Z) > 250.f)
	{
		return false;
	}
	// Line of sight from the eyes to just above the pool
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HCReportSight), false, Who);
	Params.AddIgnoredActor(this);
	return !GetWorld()->LineTraceTestByChannel(Who->GetCamera()->GetComponentLocation(), GetActorLocation() + FVector(0.f, 0.f, 25.f), ECC_Visibility, Params);
}

FText AHCRemains::GetInteractionText(const AHCCharacter* Who) const
{
	return NSLOCTEXT("HC", "Report", "Melden");
}

void AHCRemains::ExecuteInteraction(AHCCharacter* Who)
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->ReportRemains(Who, this);
	}
}

void AHCRemains::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCRemains, VictimName);
	DOREPLIFETIME(AHCRemains, VictimIdentity);
	DOREPLIFETIME(AHCRemains, bReported);
}
