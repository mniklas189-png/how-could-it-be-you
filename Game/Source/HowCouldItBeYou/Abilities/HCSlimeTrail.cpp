#include "Abilities/HCSlimeTrail.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/HCGreyboxAssets.h"

AHCSlimeTrail::AHCSlimeTrail()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Puddle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Puddle"));
	RootComponent = Puddle;
	Puddle->SetStaticMesh(HCGreybox::Cylinder());
	// About 90 cm across and 1 cm thick
	Puddle->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.01f));
	Puddle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Puddle->SetCanEverAffectNavigation(false);
	Puddle->SetCastShadow(false);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Puddle);
	Glow->SetAbsolute(false, false, true);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 9000.f)); // 90 cm above the floor (the parent is 1 % tall)
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(0.f);
	Glow->SetAttenuationRadius(450.f);
	Glow->SetLightColor(FLinearColor(0.45f, 1.f, 0.3f));
	Glow->SetCastShadows(false);
}

void AHCSlimeTrail::BeginPlay()
{
	Super::BeginPlay();
	Puddle->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.07f, 0.16f, 0.04f)));
	SetLifeSpan(LifeSeconds);
}

void AHCSlimeTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	// Short effect: flares up at once and is gone after 1.5 s; the puddle shrinks away in its last 5 s
	const float Flash = FMath::Clamp(1.f - Age / 1.5f, 0.f, 1.f);
	Glow->SetIntensity(14.f * Flash * Flash);
	const float Left = FMath::Clamp((LifeSeconds - Age) / 5.f, 0.05f, 1.f);
	Puddle->SetRelativeScale3D(FVector(0.9f * Left, 0.9f * Left, 0.01f));
	if (Age > 1.5f && Left >= 1.f)
	{
		SetActorTickInterval(0.25f);
	}
}
