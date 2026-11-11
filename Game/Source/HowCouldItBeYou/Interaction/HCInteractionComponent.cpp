#include "Interaction/HCInteractionComponent.h"

#include "Core/HCKeyBindings.h"
#include "World/HCModelAssets.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "Engine/World.h"
#include "Abilities/HCRemains.h"
#include "EngineUtils.h"
#include "Interaction/HCInteractable.h"

namespace
{
	constexpr float TraceInterval = 0.05f;   // 20x per second
	constexpr float TraceRadius = 10.f;
	constexpr float ServerRangeSlack = 60.f; // latency / bounds tolerance
	constexpr float MaxHoldMoveDistance = 60.f;

	/** Remains that can be reported from here; they have priority over everything else (docs/02 §7.11). */
	AActor* FindReportableRemains(const AHCCharacter* Character)
	{
		AHCRemains* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AHCRemains> It(Character->GetWorld()); It; ++It)
		{
			FText Reason;
			if (!It->CanInteract(Character, Reason))
			{
				continue;
			}
			const float Distance = FVector::DistSquared(It->GetActorLocation(), Character->GetActorLocation());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}
}

UHCInteractionComponent::UHCInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

AHCCharacter* UHCInteractionComponent::GetCharacter() const
{
	return Cast<AHCCharacter>(GetOwner());
}

void UHCInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AHCCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	if (Character->IsLocallyControlled())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextTraceTime && !bLocalHolding)
		{
			NextTraceTime = Now + TraceInterval;
			UpdateFocus();
		}
	}
	if (Character->HasAuthority() && ServerTarget.IsValid())
	{
		ServerTickHold();
	}
}

void UHCInteractionComponent::UpdateFocus()
{
	AHCCharacter* Character = GetCharacter();
	const UCameraComponent* Camera = Character->GetCamera();
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * UHCGameSettings::Get()->InteractionRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HCInteraction), false, Character);
	FHitResult Hit;
	AActor* NewFocus = FindReportableRemains(Character);
	if (!NewFocus && GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, HC_TRACE_INTERACTION, FCollisionShape::MakeSphere(TraceRadius), Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (HitActor && HitActor->Implements<UHCInteractable>())
		{
			NewFocus = HitActor;
		}
	}
	SetFocus(NewFocus);

	if (IHCInteractable* Interactable = Cast<IHCInteractable>(NewFocus))
	{
		FocusReason = FText::GetEmpty();
		bFocusAvailable = Interactable->CanInteract(Character, FocusReason);
	}
	else
	{
		bFocusAvailable = false;
	}
}

// The grey block of an object may be hidden behind a real model (HCModels::Dress): outline that one as well
static void SetDressOutline(AActor* Actor, bool bOn)
{
	if (Actor)
	{
		for (UActorComponent* Component : Actor->GetComponentsByTag(UPrimitiveComponent::StaticClass(), HCModels::DressTag))
		{
			CastChecked<UPrimitiveComponent>(Component)->SetRenderCustomDepth(bOn);
		}
	}
}

void UHCInteractionComponent::SetFocus(AActor* NewFocus)
{
	if (FocusedActor.Get() == NewFocus)
	{
		return;
	}
	// Custom depth is the hook for the outline post-process material (M_PP_Outline, docs/02 §10.1).
	if (IHCInteractable* Old = Cast<IHCInteractable>(FocusedActor.Get()))
	{
		if (UPrimitiveComponent* Prim = Old->GetHighlightComponent())
		{
			Prim->SetRenderCustomDepth(false);
		}
		SetDressOutline(FocusedActor.Get(), false);
	}
	FocusedActor = NewFocus;
	if (IHCInteractable* New = Cast<IHCInteractable>(NewFocus))
	{
		if (UPrimitiveComponent* Prim = New->GetHighlightComponent())
		{
			Prim->SetRenderCustomDepth(true);
		}
		SetDressOutline(NewFocus, true);
	}
}

FText UHCInteractionComponent::GetPromptText() const
{
	const IHCInteractable* Interactable = Cast<IHCInteractable>(FocusedActor.Get());
	if (!Interactable)
	{
		return FText::GetEmpty();
	}
	if (!bFocusAvailable)
	{
		return FocusReason;
	}
	return FText::Format(NSLOCTEXT("HC", "PromptKeyFmt", "[{0}] {1}"), HCKeys::ShortName(EHCBinding::Interact), Interactable->GetInteractionText(GetCharacter()));
}

float UHCInteractionComponent::GetHoldProgress() const
{
	if (!bLocalHolding || LocalHoldDuration <= 0.f)
	{
		return -1.f;
	}
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - LocalHoldStart) / LocalHoldDuration, 0.f, 1.f);
}

void UHCInteractionComponent::BeginInteract()
{
	AActor* Target = FocusedActor.Get();
	IHCInteractable* Interactable = Cast<IHCInteractable>(Target);
	if (!Interactable || !bFocusAvailable)
	{
		return;
	}
	const float Hold = Interactable->GetHoldDuration(GetCharacter());
	if (Hold > 0.f)
	{
		bLocalHolding = true;
		LocalHoldStart = GetWorld()->GetTimeSeconds();
		LocalHoldDuration = Hold;
	}
	ServerBeginInteract(Target);
}

void UHCInteractionComponent::EndInteract()
{
	if (bLocalHolding)
	{
		bLocalHolding = false;
		ServerEndInteract();
	}
}

bool UHCInteractionComponent::ServerValidate(AActor* Target, FText& OutReason) const
{
	AHCCharacter* Character = GetCharacter();
	IHCInteractable* Interactable = Cast<IHCInteractable>(Target);
	if (!Character || !Interactable)
	{
		return false;
	}
	const FVector Eye = Character->GetCamera()->GetComponentLocation();
	const FBox Bounds = Target->GetComponentsBoundingBox();
	const float MaxRange = UHCGameSettings::Get()->InteractionRange + ServerRangeSlack;
	if (!Interactable->ChecksRangeItself() && Bounds.ComputeSquaredDistanceToPoint(Eye) > FMath::Square(MaxRange))
	{
		OutReason = NSLOCTEXT("HC", "TooFar", "Zu weit entfernt");
		return false;
	}
	return Interactable->CanInteract(Character, OutReason);
}

void UHCInteractionComponent::ServerBeginInteract_Implementation(AActor* Target)
{
	FText Reason;
	if (!ServerValidate(Target, Reason))
	{
		UE_LOG(LogHC, Verbose, TEXT("Interaction with %s rejected: %s"), *GetNameSafe(Target), *Reason.ToString());
		ClientInteractEnded(false);
		return;
	}
	IHCInteractable* Interactable = Cast<IHCInteractable>(Target);
	AHCCharacter* Character = GetCharacter();
	const float Hold = Interactable->GetHoldDuration(Character);
	if (Hold <= 0.f)
	{
		Interactable->ExecuteInteraction(Character);
		return;
	}
	ServerTarget = Target;
	ServerHoldEnd = GetWorld()->GetTimeSeconds() + Hold;
	ServerHoldStartLocation = Character->GetActorLocation();
}

void UHCInteractionComponent::ServerEndInteract_Implementation()
{
	if (ServerTarget.IsValid())
	{
		ServerTarget.Reset();
		ClientInteractEnded(false);
	}
}

void UHCInteractionComponent::ServerTickHold()
{
	AHCCharacter* Character = GetCharacter();
	AActor* Target = ServerTarget.Get();
	FText Reason;
	const bool bMoved = FVector::DistSquared2D(Character->GetActorLocation(), ServerHoldStartLocation) > FMath::Square(MaxHoldMoveDistance);
	if (bMoved || !ServerValidate(Target, Reason))
	{
		ServerTarget.Reset();
		ClientInteractEnded(false);
		return;
	}
	if (GetWorld()->GetTimeSeconds() >= ServerHoldEnd)
	{
		ServerTarget.Reset();
		Cast<IHCInteractable>(Target)->ExecuteInteraction(Character);
		ClientInteractEnded(true);
	}
}

void UHCInteractionComponent::ClientInteractEnded_Implementation(bool bCompleted)
{
	bLocalHolding = false;
	NextTraceTime = 0.f;
}
