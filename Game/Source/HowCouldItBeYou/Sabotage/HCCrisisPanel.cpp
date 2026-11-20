#include "Sabotage/HCCrisisPanel.h"

#include "Characters/HCCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "World/HCGreyboxAssets.h"

AHCCrisisPanel::AHCCrisisPanel()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;

	// Actor origin = centre of the panel; its back to the wall, the front along +X. The root is not scaled: a spawn
	// transform would reset the scale of a scaled root to 1.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Block = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Block"));
	Block->SetupAttachment(RootComponent);
	Block->SetStaticMesh(HCGreybox::Cube());
	Block->SetRelativeScale3D(FVector(0.2f, 0.8f, 1.f));
	Block->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Block->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Block);

	// The big lever sticks out of the front, a little below the middle (cm)
	Handle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Handle"));
	Handle->SetupAttachment(RootComponent);
	Handle->SetStaticMesh(HCGreybox::Cube());
	Handle->SetRelativeLocation(FVector(15.f, 0.f, -12.f));
	Handle->SetRelativeScale3D(FVector(0.12f, 0.44f, 0.1f));
	Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Alarm = CreateDefaultSubobject<UPointLightComponent>(TEXT("Alarm"));
	Alarm->SetupAttachment(RootComponent);
	Alarm->SetRelativeLocation(FVector(32.f, 0.f, 62.f));
	Alarm->SetIntensityUnits(ELightUnits::Candelas);
	Alarm->SetIntensity(0.f);
	Alarm->SetAttenuationRadius(520.f);
	Alarm->SetLightColor(FLinearColor(1.f, 0.12f, 0.06f));
	Alarm->SetCastShadows(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(10.6f, 0.f, 30.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(9.f);
	Label->SetTextRenderColor(FColor(220, 220, 220));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AHCCrisisPanel::Configure(EHCCrisis InCrisis, uint8 InIndex)
{
	Crisis = InCrisis;
	PanelIndex = InIndex;
}

void AHCCrisisPanel::BeginPlay()
{
	Super::BeginPlay();
	Block->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.16f, 0.17f, 0.18f)));
	Handle->SetMaterial(0, HCGreybox::MakeColored(this, Crisis == EHCCrisis::Oxygen ? FLinearColor(0.1f, 0.35f, 0.85f) : FLinearColor(0.9f, 0.55f, 0.05f)));
	Label->SetText(GetPanelName().ToUpper());
}

FText AHCCrisisPanel::CrisisName(EHCCrisis InCrisis)
{
	return InCrisis == EHCCrisis::Oxygen ? NSLOCTEXT("HC", "CrisisOxygen", "Sauerstoffausfall") : NSLOCTEXT("HC", "CrisisMeltdown", "Kernschmelze");
}

FText AHCCrisisPanel::CrisisTask(EHCCrisis InCrisis)
{
	return InCrisis == EHCCrisis::Oxygen ? NSLOCTEXT("HC", "CrisisOxygenTask", "Beide Sauerstoffventile gleichzeitig halten")
		: NSLOCTEXT("HC", "CrisisMeltdownTask", "Beide Notkühlungen im Reaktor gleichzeitig halten");
}

FText AHCCrisisPanel::GetPanelName() const
{
	const FText Letter = FText::FromString(PanelIndex == 0 ? TEXT("A") : TEXT("B"));
	return FText::Format(Crisis == EHCCrisis::Oxygen ? NSLOCTEXT("HC", "CrisisValveFmt", "Sauerstoffventil {0}") : NSLOCTEXT("HC", "CrisisCoolingFmt", "Notkühlung {0}"), Letter);
}

bool AHCCrisisPanel::IsActive() const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	return GS && Crisis != EHCCrisis::None && GS->GetCrisis() == Crisis && GS->GetMatchPhase() == EHCMatchPhase::Playing;
}

bool AHCCrisisPanel::IsHeld() const
{
	for (const TWeakObjectPtr<AHCCharacter>& Holder : Holders)
	{
		if (Holder.IsValid() && Holder->IsAlive())
		{
			return true;
		}
	}
	return false;
}

bool AHCCrisisPanel::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	// Without its crisis it is just a panel on the wall: no hint at all
	return Who && Who->CanAct() && IsActive();
}

FText AHCCrisisPanel::GetInteractionText(const AHCCharacter* Who) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	return FText::Format(NSLOCTEXT("HC", "CrisisPanelPromptFmt", "{0} halten  ·  {1}/2 gehalten"), GetPanelName(), GS ? GS->GetCrisisPanelsHeld() : 0);
}

void AHCCrisisPanel::OnHoldChanged(AHCCharacter* Who, bool bHolding)
{
	Holders.RemoveAll([Who](const TWeakObjectPtr<AHCCharacter>& Holder) { return !Holder.IsValid() || Holder.Get() == Who; });
	if (bHolding && Who)
	{
		Holders.Add(Who);
	}
	UE_LOG(LogHCMatch, Log, TEXT("Crisis panel %s: %s %s"), *GetPanelName().ToString(), Who && Who->GetPlayerState() ? *Who->GetPlayerState()->GetPlayerName() : TEXT("?"),
		bHolding ? TEXT("holds it") : TEXT("let go"));
}

float AHCCrisisPanel::GetHoldProgress(const AHCCharacter* Who) const
{
	// The bar fills only while both panels are held, and runs back to nothing when one lets go
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	return GS ? GS->GetCrisisFix() : 0.f;
}

void AHCCrisisPanel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const bool bActive = IsActive();
	if (GetNetMode() != NM_DedicatedServer)
	{
		// Flashes twice a second while its crisis runs; steady and dark otherwise
		const float Time = GetWorld()->GetTimeSeconds();
		const float Flash = bActive ? FMath::Square(FMath::Max(0.f, FMath::Sin(Time * 2.f * PI * 1.6f))) : 0.f;
		Alarm->SetIntensity(60.f * Flash);
		Label->SetTextRenderColor(bActive ? FColor(255, 70, 50) : FColor(200, 200, 200));
	}
	if (!bActive && HasAuthority())
	{
		Holders.Reset();
	}
	SetActorTickInterval(bActive ? 0.f : 0.5f);
}

void AHCCrisisPanel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCCrisisPanel, Crisis);
	DOREPLIFETIME(AHCCrisisPanel, PanelIndex);
}
