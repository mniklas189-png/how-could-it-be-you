#include "Abilities/HCSlimeTrail.h"

#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "World/HCEffectAssets.h"
#include "World/HCGreyboxAssets.h"

namespace
{
	// Shine of the slime itself (emissive), on top of the green light: strong while it is fresh, then a faint glimmer
	constexpr float SlimeGlowFresh = 9.f;
	constexpr float SlimeGlowAfter = 0.25f;
	constexpr float FlashSeconds = 1.5f;
	constexpr float DrySeconds = 5.f;
}

AHCSlimeTrail::AHCSlimeTrail()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	// Actor origin = point on the floor
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Spot"));

	Puddle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Puddle"));
	Puddle->SetupAttachment(RootComponent);
	Puddle->SetStaticMesh(HCGreybox::Cylinder());
	// About 90 cm across and 1 cm thick
	Puddle->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.01f));
	Puddle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Puddle->SetCanEverAffectNavigation(false);
	Puddle->SetCastShadow(false);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(RootComponent);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(0.f);
	Glow->SetAttenuationRadius(450.f);
	Glow->SetLightColor(FLinearColor(0.45f, 1.f, 0.3f));
	Glow->SetCastShadows(false);
}

void AHCSlimeTrail::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(LifeSeconds);
	UMaterialInterface* SlimeMaterial = HCEffects::Load<UMaterialInterface>(TEXT("M_SlimeDecal"));
	if (!SlimeMaterial)
	{
		Puddle->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.07f, 0.16f, 0.04f)));
		return;
	}
	Puddle->SetVisibility(false);

	// One pool where it stood (picture 0 or 1) and two small splashes next to it (picture 2). Which pool and how
	// everything is turned follows from the place, so every machine shows the same slime.
	const FVector Place = GetActorLocation();
	FRandomStream Random(FMath::RoundToInt32(Place.X / 10.0) * 23 + FMath::RoundToInt32(Place.Y / 10.0) * 41);
	struct FSplash
	{
		int32 Picture;
		double Size;
		double Distance;
	};
	TArray<FSplash, TInlineAllocator<3>> Splashes;
	Splashes.Add({ Random.RandRange(0, 1), 80.f, 0.f });
	Splashes.Add({ 2, Random.FRandRange(30.f, 38.f), Random.FRandRange(65.f, 90.f) });
	Splashes.Add({ 2, Random.FRandRange(22.f, 28.f), Random.FRandRange(75.f, 110.f) });
	for (const FSplash& Splash : Splashes)
	{
		UDecalComponent* Decal = NewObject<UDecalComponent>(this);
		Decal->SetupAttachment(RootComponent);
		// X is the depth of the projection: thin, so it stays on the floor. Y/Z are half the width.
		Decal->DecalSize = FVector(14.f, Splash.Size, Splash.Size);
		const float Away = Random.FRandRange(0.f, 2.f * PI);
		Decal->SetRelativeLocationAndRotation(FVector(FMath::Cos(Away) * Splash.Distance, FMath::Sin(Away) * Splash.Distance, 0.f),
			FRotator(-90.f, Random.FRandRange(0.f, 360.f), 0.f));
		// Over blood that already lies there
		Decal->SortOrder = 1;
		UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(SlimeMaterial, this);
		if (UTexture* Picture = HCEffects::Load<UTexture>(FString::Printf(TEXT("T_Slime_%d"), Splash.Picture)))
		{
			Look->SetTextureParameterValue(TEXT("Splat"), Picture);
		}
		if (UTexture* Bumps = HCEffects::Load<UTexture>(FString::Printf(TEXT("T_Slime_%d_N"), Splash.Picture)))
		{
			Look->SetTextureParameterValue(TEXT("Bumps"), Bumps);
		}
		Look->SetScalarParameterValue(TEXT("Glow"), SlimeGlowFresh);
		Decal->SetDecalMaterial(Look);
		Decal->RegisterComponent();
		SlimeLooks.Add(Look);
	}
}

void AHCSlimeTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	// Short effect: flares up at once and is gone after 1.5 s; in its last 5 s the slime dries up and draws together
	const float Flash = FMath::Clamp(1.f - Age / FlashSeconds, 0.f, 1.f);
	Glow->SetIntensity(14.f * Flash * Flash);
	const float Left = FMath::Clamp((LifeSeconds - Age) / DrySeconds, 0.f, 1.f);
	for (UMaterialInstanceDynamic* Look : SlimeLooks)
	{
		Look->SetScalarParameterValue(TEXT("Glow"), FMath::Lerp(SlimeGlowAfter, SlimeGlowFresh, Flash * Flash) * Left);
		Look->SetScalarParameterValue(TEXT("Fade"), Left);
	}
	const float Size = SlimeLooks.IsEmpty() ? FMath::Max(Left, 0.05f) : FMath::Lerp(0.6f, 1.f, Left);
	RootComponent->SetRelativeScale3D(FVector(Size, Size, 1.f));
	// Nothing changes between the flash and the drying
	SetActorTickInterval(Age > FlashSeconds && Left >= 1.f ? 0.25f : 0.f);
}
