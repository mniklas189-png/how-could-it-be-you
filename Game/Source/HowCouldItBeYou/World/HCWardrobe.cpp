#include "World/HCWardrobe.h"

#include "Characters/HCCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCModelAssets.h"

AHCWardrobe::AHCWardrobe()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Block = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Block"));
	Block->SetupAttachment(Root);
	Block->SetStaticMesh(HCGreybox::Cube());
	// Front along +X; the actor stands on the floor
	Block->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	Block->SetRelativeScale3D(FVector(0.5f, 1.f, 2.f));
	Block->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Block->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Block);
}

void AHCWardrobe::BeginPlay()
{
	Super::BeginPlay();
	// Two lockers of the game's own models side by side (Tools\Models); without them the grey block stays
	const FBox Left = HCModels::Dress(this, TEXT("Locker"), FBox(FVector(-25.0, -50.0, 0.0), FVector(25.0, 0.0, 200.0)), false);
	const FBox Right = HCModels::Dress(this, TEXT("Locker"), FBox(FVector(-25.0, 0.0, 0.0), FVector(25.0, 50.0, 200.0)), false);
	if (Left.IsValid || Right.IsValid)
	{
		Block->SetVisibility(false);
	}
	else
	{
		Block->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.55f, 0.55f, 0.52f)));
	}
}

bool AHCWardrobe::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!Who || !Who->CanAct() || !GS)
	{
		return false;
	}
	if (GS->GetMatchPhase() != EHCMatchPhase::Lobby)
	{
		OutReason = NSLOCTEXT("HC", "WardrobeOnlyLobby", "Umziehen geht nur in der Lobby");
		return false;
	}
	return true;
}

FText AHCWardrobe::GetInteractionText(const AHCCharacter* Who) const
{
	return NSLOCTEXT("HC", "WardrobePrompt", "Kleiderschrank: Aussehen ändern");
}

void AHCWardrobe::ExecuteInteraction(AHCCharacter* Who)
{
	if (AHCPlayerController* PC = Who ? Cast<AHCPlayerController>(Who->GetController()) : nullptr)
	{
		PC->ClientOpenWardrobe(this);
	}
}

FTransform AHCWardrobe::GetMirrorCamera(const AActor* Player, float& OutFieldOfView) const
{
	const FVector PlayerLocation = Player ? Player->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.0;
	const double Floor = GetActorLocation().Z;
	FVector Away = PlayerLocation - GetActorLocation();
	Away.Z = 0.0;
	Away = Away.GetSafeNormal(UE_SMALL_NUMBER, GetActorForwardVector());
	// The camera's left, seen from the camera looking back at the player
	const FVector Left = FVector::CrossProduct(FVector::UpVector, Away);
	const FVector Feet(PlayerLocation.X, PlayerLocation.Y, Floor);

	// 3 m in front of the player, or less when a wall or a table stands there
	double Distance = 300.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HCWardrobeMirror), false, this);
	Params.AddIgnoredActor(Player);
	FHitResult Hit;
	const FVector From = Feet + FVector(0.0, 0.0, 120.0);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, From, From + Away * Distance, ECC_Visibility, Params))
	{
		Distance = FMath::Max(110.0, Hit.Distance - 30.0);
	}
	// Wide enough for the whole figure (about 2.1 m high in a 16:9 picture)
	const double HalfWide = 105.0 / Distance * 16.0 / 9.0;
	OutFieldOfView = FMath::Clamp(static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(HalfWide))), 50.f, 100.f);
	const double HalfWidth = Distance * FMath::Tan(FMath::DegreesToRadians(OutFieldOfView * 0.5));
	// The view aims a bit to the right of the figure, so it stands in the left third of the picture
	const FVector Eye = From + Away * Distance;
	const FVector Look = Feet + FVector(0.0, 0.0, 95.0) - Left * (0.36 * HalfWidth);
	return FTransform((Look - Eye).Rotation(), Eye);
}
