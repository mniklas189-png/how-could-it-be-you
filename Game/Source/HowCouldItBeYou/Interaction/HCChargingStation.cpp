#include "Interaction/HCChargingStation.h"

#include "Characters/HCCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "Engine/CollisionProfile.h"
#include "World/HCGreyboxAssets.h"

AHCChargingStation::AHCChargingStation()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetDormancy(DORM_DormantAll);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetStaticMesh(HCGreybox::Cube());
	// 40 cm deep, 80 cm wide, 140 cm tall, standing on the floor with its back to the wall
	Body->SetRelativeScale3D(FVector(0.4f, 0.8f, 1.4f));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Body->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Block);

	Screen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Screen"));
	Screen->SetupAttachment(Body);
	Screen->SetStaticMesh(HCGreybox::Cube());
	Screen->SetRelativeLocation(FVector(51.f, 0.f, 20.f));
	Screen->SetRelativeScale3D(FVector(0.05f, 0.6f, 0.2f));
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Body);
	Glow->SetRelativeLocation(FVector(120.f, 0.f, 20.f));
	Glow->SetIntensity(800.f);
	Glow->SetAttenuationRadius(250.f);
	Glow->SetLightColor(FLinearColor(0.2f, 1.f, 0.4f));
	Glow->SetCastShadows(false);
}

bool AHCChargingStation::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	if (!Who || !Who->CanAct())
	{
		OutReason = NSLOCTEXT("HC", "CantActNow", "Gerade nicht möglich");
		return false;
	}
	if (Who->GetBatteryFraction() >= 0.999f)
	{
		OutReason = NSLOCTEXT("HC", "BatteryFull", "Akku ist voll");
		return false;
	}
	return true;
}

FText AHCChargingStation::GetInteractionText(const AHCCharacter* Who) const
{
	return NSLOCTEXT("HC", "ChargeFlashlight", "Taschenlampe laden (halten)");
}

float AHCChargingStation::GetHoldDuration(const AHCCharacter* Who) const
{
	// Scale with how empty the battery is: empty = full ChargingStationSeconds
	const float Missing = Who ? 1.f - Who->GetBatteryFraction() : 1.f;
	return FMath::Max(0.5f, UHCGameSettings::Get()->ChargingStationSeconds * Missing);
}

void AHCChargingStation::ExecuteInteraction(AHCCharacter* Who)
{
	if (Who)
	{
		Who->Server_SetBatteryFull();
		UE_LOG(LogHC, Log, TEXT("%s charged flashlight at %s"), *GetNameSafe(Who), *GetName());
	}
}

UPrimitiveComponent* AHCChargingStation::GetHighlightComponent() const
{
	return Body;
}
