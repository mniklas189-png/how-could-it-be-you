#include "World/HCModelAssets.h"

#include "Core/HCLog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundBase.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

UHCModelSet* HCModels::Get()
{
	static TWeakObjectPtr<UHCModelSet> Cached;
	static bool bMissing = false;
	if (UHCModelSet* Set = Cached.Get())
	{
		return Set;
	}
	if (bMissing)
	{
		return nullptr;
	}
	const TCHAR* Package = TEXT("/Game/HowCouldItBeYou/Models/DA_Models");
	if (!FPackageName::DoesPackageExist(Package))
	{
		bMissing = true;
		UE_LOG(LogHC, Log, TEXT("No model set (%s): using the greybox figures"), Package);
		return nullptr;
	}
	UHCModelSet* Set = Cast<UHCModelSet>(FSoftObjectPath(TEXT("/Game/HowCouldItBeYou/Models/DA_Models.DA_Models")).TryLoad());
	Cached = Set;
	bMissing = Set == nullptr;
	if (Set)
	{
		UE_LOG(LogHC, Log, TEXT("Model set loaded: character %s, monster %s"), *GetNameSafe(Set->CharacterMesh), *GetNameSafe(Set->MonsterMesh));
	}
	return Set;
}

const FName HCModels::DressTag(TEXT("HC.Dress"));

UStaticMesh* UHCModelSet::FindProp(FName Key, float* OutYaw) const
{
	if (OutYaw)
	{
		*OutYaw = PropYaws.FindRef(Key);
	}
	return Props.FindRef(Key);
}

FBox HCModels::Dress(AActor* Owner, FName Key, const FBox& Box, bool bAgainstBack, FVector* OutPivot)
{
	const UHCModelSet* Models = Get();
	float Yaw = 0.f;
	UStaticMesh* Mesh = Models ? Models->FindProp(Key, &Yaw) : nullptr;
	USceneComponent* Parent = Owner ? Owner->GetRootComponent() : nullptr;
	if (!Mesh || !Parent)
	{
		return FBox(ForceInit);
	}
	const FQuat Turn = FRotator(0.f, Yaw, 0.f).Quaternion();
	const FBox Turned = Mesh->GetBoundingBox().TransformBy(FTransform(Turn));
	const FVector Size = Turned.GetSize().ComponentMax(FVector(1.0));
	const FVector Room = Box.GetSize();
	const double Scale = FMath::Min3(Room.X / Size.X, Room.Y / Size.Y, Room.Z / Size.Z);
	const FVector Half = Size * Scale * 0.5;
	const FVector Target(bAgainstBack ? Box.Min.X + Half.X : Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z + Half.Z);
	const FVector At = Target - Turned.GetCenter() * Scale;

	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
	Component->SetMobility(Parent->Mobility);
	Component->SetupAttachment(Parent);
	// The grey blocks are scaled cubes; the stand-in keeps its own even scale
	Component->SetUsingAbsoluteScale(true);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->ComponentTags.Add(DressTag);
	Component->RegisterComponent();
	const FTransform ActorSpace(Owner->GetActorQuat(), Owner->GetActorLocation());
	Component->SetWorldScale3D(FVector(Scale));
	Component->SetWorldLocationAndRotation(ActorSpace.TransformPosition(At), ActorSpace.GetRotation() * Turn);
	if (OutPivot)
	{
		*OutPivot = At;
	}
	return FBox(Target - Half, Target + Half);
}
