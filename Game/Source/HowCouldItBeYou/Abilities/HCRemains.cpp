#include "Abilities/HCRemains.h"

#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCLog.h"
#include "Engine/SkeletalMesh.h"
#include "World/HCModelAssets.h"
#include "Engine/Texture.h"
#include "Math/RandomStream.h"
#include "Misc/PackageName.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameSettings.h"
#include "Core/HCTypes.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "World/HCEffectAssets.h"
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
	// Blood splatter on the floor instead of the flat red disc: one big pool and two smaller splashes next to it.
	// Which pictures and how they are turned follows from the place.
	if (UMaterialInterface* Splatter = HCEffects::Load<UMaterialInterface>(TEXT("M_BloodDecal")))
	{
		const FVector Place = GetActorLocation();
		FRandomStream Random(FMath::RoundToInt32(Place.X / 10.0) * 31 + FMath::RoundToInt32(Place.Y / 10.0) * 17);
		const int32 First = Random.RandRange(0, 2);
		const float Sizes[] = { 135.f, 64.f, 50.f };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sizes); ++Index)
		{
			UDecalComponent* Decal = NewObject<UDecalComponent>(this);
			Decal->SetupAttachment(RootComponent);
			// X is the depth of the projection: thin, so it stays on the floor
			Decal->DecalSize = FVector(14.f, Sizes[Index], Sizes[Index]);
			const float Away = Random.FRandRange(0.f, 2.f * PI);
			const float Distance = Index == 0 ? 0.f : Random.FRandRange(115.f, 165.f);
			Decal->SetRelativeLocationAndRotation(FVector(FMath::Cos(Away) * Distance, FMath::Sin(Away) * Distance, 0.f),
				FRotator(-90.f, Random.FRandRange(0.f, 360.f), 0.f));
			UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Splatter, this);
			if (UTexture* Picture = HCEffects::Load<UTexture>(FString::Printf(TEXT("T_Blood_%d"), (First + Index) % 3)))
			{
				Look->SetTextureParameterValue(TEXT("Splat"), Picture);
			}
			Decal->SetDecalMaterial(Look);
			Decal->RegisterComponent();
		}
		// The disc is no longer drawn, but still gives the outline when you look at the remains
		Pool->SetRenderInMainPass(false);
		Pool->SetRenderInDepthPass(false);
	}
	// The body itself. Every computer lets it drop by itself, so it may lie a little differently for each player -
	// what counts (where it is, who it was, that it can be reported) is the same for all.
	const UHCModelSet* Models = HCModels::Get();
	if (Models && Models->CharacterMesh && GetNetMode() != NM_DedicatedServer)
	{
		const FVector Place = GetActorLocation();
		FRandomStream Random(FMath::RoundToInt32(Place.X / 10.0) * 13 + FMath::RoundToInt32(Place.Y / 10.0) * 29);
		const float Yaw = Random.FRandRange(0.f, 360.f);
		Body = NewObject<USkeletalMeshComponent>(this);
		Body->SetupAttachment(RootComponent);
		Body->SetSkeletalMesh(Models->CharacterMesh);
		Body->SetAnimationMode(EAnimationMode::AnimationCustomMode);
		Body->SetReceivesDecals(false);
		Body->SetCanEverAffectNavigation(false);
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 3.f), FRotator(0.f, Yaw, 0.f));
		Body->RegisterComponent();
		if (Body->GetPhysicsAsset())
		{
			// Never in anybody's way: it only lies on floors and against walls and props
			Body->SetCollisionProfileName(TEXT("Ragdoll"));
			Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			Body->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Ignore);
			Body->SetSimulatePhysics(true);
			Body->AddImpulse(FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, -140.f, 0.f)), NAME_None, true);
		}
		else
		{
			// No physics for this model: laid flat on its back
			Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 14.f), FRotator(0.f, Yaw, -90.f));
		}
	}
	OnRep_Victim();
}

void AHCRemains::OnRep_Victim()
{
	// Victims are never disguised, so this is always the real name and identity (docs/02 §7.9)
	NameLabel->SetText(FText::FromString(VictimName));
	// The suit of the body carries the colour of the victim, like the suit of the living player did
	const UHCModelSet* Models = HCModels::Get();
	if (Body && Models)
	{
		for (const FName Slot : Models->TintSlots)
		{
			const int32 SlotIndex = Body->GetMaterialIndex(Slot);
			if (UMaterialInstanceDynamic* Tinted = SlotIndex != INDEX_NONE ? Body->CreateDynamicMaterialInstance(SlotIndex) : nullptr)
			{
				Tinted->SetVectorParameterValue(Models->TintParameter, HCIdentity::Color(VictimIdentity));
			}
		}
	}
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
	return NSLOCTEXT("HC", "Report", "Leiche melden");
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
