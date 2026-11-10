#include "Meeting/HCEmergencyButton.h"

#include "Characters/HCCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/HCGreyboxAssets.h"

AHCEmergencyButton::AHCEmergencyButton()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetDormancy(DORM_DormantAll);

	Pedestal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pedestal"));
	RootComponent = Pedestal;
	Pedestal->SetStaticMesh(HCGreybox::Cube());
	// 70 x 70 cm, 100 cm tall, the actor origin is its centre
	Pedestal->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.f));
	Pedestal->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Pedestal->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Block);

	Button = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Button"));
	Button->SetupAttachment(Pedestal);
	Button->SetStaticMesh(HCGreybox::Cylinder());
	Button->SetRelativeLocation(FVector(0.f, 0.f, 56.f));
	// Relative to the scaled pedestal: 42 cm across, 12 cm tall
	Button->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.12f));
	Button->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Pedestal);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	Glow->SetIntensity(1200.f);
	Glow->SetAttenuationRadius(350.f);
	Glow->SetLightColor(FLinearColor(1.f, 0.15f, 0.1f));
	Glow->SetCastShadows(false);
}

void AHCEmergencyButton::BeginPlay()
{
	Super::BeginPlay();
	Pedestal->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.12f, 0.13f, 0.15f)));
	Button->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.85f, 0.06f, 0.04f)));
}

bool AHCEmergencyButton::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!Who || !PS || !GS || !Who->CanAct() || !PS->IsAlive())
	{
		OutReason = NSLOCTEXT("HC", "CantActNow", "Gerade nicht möglich");
		return false;
	}
	if (GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		OutReason = NSLOCTEXT("HC", "ButtonOnlyInRound", "Nur während einer Runde");
		return false;
	}
	if (PS->GetEmergencyMeetingsLeft() <= 0)
	{
		OutReason = NSLOCTEXT("HC", "ButtonUsedUp", "Kein Notfall-Meeting mehr übrig");
		return false;
	}
	const double Wait = GS->GetEmergencyReadyServerTime() - GS->GetServerWorldTimeSeconds();
	if (Wait > 0.0)
	{
		OutReason = FText::Format(NSLOCTEXT("HC", "ButtonLocked", "Notfall-Knopf gesperrt ({0} s)"), FMath::CeilToInt32(Wait));
		return false;
	}
	return true;
}

FText AHCEmergencyButton::GetInteractionText(const AHCCharacter* Who) const
{
	const AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
	return FText::Format(NSLOCTEXT("HC", "ButtonPrompt", "Notfall-Meeting einberufen (noch {0})"), PS ? PS->GetEmergencyMeetingsLeft() : 0);
}

void AHCEmergencyButton::ExecuteInteraction(AHCCharacter* Who)
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->RequestEmergencyMeeting(Who);
	}
}

UPrimitiveComponent* AHCEmergencyButton::GetHighlightComponent() const
{
	return Pedestal;
}

FVector AHCEmergencyButton::GetStandLocation() const
{
	return GetActorLocation() + GetActorForwardVector() * 130.f + FVector(0.f, 0.f, 42.f);
}
