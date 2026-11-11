#include "Sabotage/HCFuseBox.h"

#include "Characters/HCCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameMode.h"
#include "Core/HCLog.h"
#include "World/HCModelAssets.h"
#include "Core/HCMatchRules.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Sabotage/HCPowerSector.h"
#include "Sabotage/HCSabotageManager.h"
#include "World/HCGreyboxAssets.h"

AHCFuseBox::AHCFuseBox()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetDormancy(DORM_DormantAll);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetStaticMesh(HCGreybox::Cube());
	// Wall box: 20 cm deep, 60 cm wide, 80 cm tall; the actor origin is its centre (130 cm above the floor)
	Body->SetRelativeScale3D(FVector(0.2f, 0.6f, 0.8f));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Body->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Block);

	Lamp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Body);
	Lamp->SetStaticMesh(HCGreybox::Cube());
	Lamp->SetRelativeLocation(FVector(52.f, 0.f, 30.f));
	Lamp->SetRelativeScale3D(FVector(0.1f, 0.5f, 0.12f));
	Lamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Body);
	Glow->SetRelativeLocation(FVector(200.f, 0.f, 20.f)); // 40 cm in front of the box (the parent is 20 % deep)
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(1.5f);
	Glow->SetAttenuationRadius(260.f);
	Glow->SetCastShadows(false);
}

void AHCFuseBox::BeginPlay()
{
	Super::BeginPlay();
	Body->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.55f, 0.45f, 0.05f)));
	LampMaterial = HCGreybox::MakeColored(this, FLinearColor::Green);
	Lamp->SetMaterial(0, LampMaterial);
	// A real switch box; it hangs a little in front of the wall, whose real panels have some depth
	const FBox Fitted = HCModels::Dress(this, TEXT("FuseBox"), FBox(FVector(-4.0, -30.0, -60.0), FVector(26.0, 30.0, 50.0)));
	if (Fitted.IsValid)
	{
		Body->SetVisibility(false); // stays as collision
		const FVector Block = Body->GetRelativeScale3D();
		// The lamp is a strip above the box
		Lamp->SetRelativeLocation(FVector(Fitted.GetCenter().X / Block.X, 0.0, (Fitted.Max.Z + 9.0) / Block.Z));
	}
}

bool AHCFuseBox::IsSectorPowered() const
{
	const AHCPowerSector* Sector = AHCPowerSector::Find(GetWorld(), SectorIndex);
	return !Sector || Sector->IsPowered();
}

void AHCFuseBox::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Green while the power is on, blinking red during an outage, so the box can be found in the dark
	const bool bPowered = IsSectorPowered();
	const bool bBlinkOn = FMath::Fmod(GetWorld()->GetTimeSeconds(), 0.8f) < 0.4f;
	const FLinearColor Color = bPowered ? FLinearColor(0.05f, 0.8f, 0.15f) : bBlinkOn ? FLinearColor(1.f, 0.05f, 0.02f) : FLinearColor(0.15f, 0.f, 0.f);
	if (LampMaterial)
	{
		LampMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	Glow->SetLightColor(Color);
	Glow->SetIntensity(bPowered ? 0.6f : bBlinkOn ? 4.f : 0.2f);
}

FVector AHCFuseBox::GetStandLocation() const
{
	return GetActorLocation() + GetActorForwardVector() * 110.f + FVector(0.f, 0.f, -38.f);
}

bool AHCFuseBox::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	if (!Who || !Who->CanAct())
	{
		OutReason = NSLOCTEXT("HC", "CantActNow", "Gerade nicht möglich");
		return false;
	}
	if (IsSectorPowered())
	{
		OutReason = NSLOCTEXT("HC", "FuseBoxFine", "Strom ist an");
		return false;
	}
	return true;
}

FText AHCFuseBox::GetInteractionText(const AHCCharacter* Who) const
{
	return FText::Format(NSLOCTEXT("HC", "FuseBoxPrompt", "Sicherungen Sektor {0} einschalten (halten)"), AHCPowerSector::GetDisplayName(SectorIndex));
}

float AHCFuseBox::GetHoldDuration(const AHCCharacter* Who) const
{
	return 4.f;
}

void AHCFuseBox::ExecuteInteraction(AHCCharacter* Who)
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->GetSabotage()->RepairPower(SectorIndex, Who);
	}
}

UPrimitiveComponent* AHCFuseBox::GetHighlightComponent() const
{
	return Body;
}

void AHCFuseBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCFuseBox, SectorIndex);
}
