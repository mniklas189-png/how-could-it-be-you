#include "Abilities/HCKillComponent.h"

#include "Abilities/HCKillRules.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UHCKillComponent::UHCKillComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

bool UHCKillComponent::IsOwnerLocalMonster() const
{
	const AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled())
	{
		return false;
	}
	// The own role is the only role a client really knows (docs/02 §2.5)
	const AHCPlayerState* PS = Character->GetPlayerState<AHCPlayerState>();
	return PS && PS->GetRoleInfo().Role == EHCRole::Monster;
}

float UHCKillComponent::GetCooldownRemaining() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Max(0.0, KillReadyServerTime - GS->GetServerWorldTimeSeconds()));
}

void UHCKillComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextSearchTime)
	{
		return;
	}
	NextSearchTime = Now + 0.1f;
	if (IsOwnerLocalMonster())
	{
		UpdateLocalTarget();
	}
	else
	{
		LocalTarget.Reset();
	}
}

void UHCKillComponent::UpdateLocalTarget()
{
	AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	LocalTarget.Reset();
	if (!Character->CanAct() || !GS || GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		return;
	}
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const FVector Start = Character->GetCamera()->GetComponentLocation();
	const FVector End = Start + Character->GetCamera()->GetForwardVector() * Settings->KillRange;

	// Other players are found by their capsule (object type Pawn)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HCKillTarget), false, Character);
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(18.f), Params))
	{
		return;
	}
	AHCCharacter* Target = Cast<AHCCharacter>(Hit.GetActor());
	if (!Target || Target->IsKillLocked() || FVector::Dist(Target->GetActorLocation(), Character->GetActorLocation()) > Settings->KillRange)
	{
		return;
	}
	// Nothing in between (walls, closed doors)
	Params.AddIgnoredActor(Target);
	if (GetWorld()->LineTraceTestByChannel(Start, Target->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params))
	{
		return;
	}
	LocalTarget = Target;
}

void UHCKillComponent::TryKill()
{
	if (!IsOwnerLocalMonster() || GetCooldownRemaining() > 0.f)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRequestTime < 0.5)
	{
		return;
	}
	if (AHCCharacter* Target = LocalTarget.Get())
	{
		LastRequestTime = Now;
		ServerRequestKill(Target);
	}
}

void UHCKillComponent::ServerRequestKill_Implementation(AHCCharacter* Target)
{
	AHCCharacter* Attacker = Cast<AHCCharacter>(GetOwner());
	AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	if (!Attacker || !GM)
	{
		return;
	}
	const EHCKillReject Reject = GM->RequestKill(Attacker, Target);
	if (Reject != EHCKillReject::None)
	{
		// Not punished: with ping an honest request can be just invalid (docs/02 §2.6). Only the monster gets a hint.
		UE_LOG(LogHCMatch, Log, TEXT("Kill request of %s rejected: %s"), *GetNameSafe(Attacker), FHCKillRules::ToString(Reject));
		if (AHCPlayerController* PC = Cast<AHCPlayerController>(Attacker->GetController()))
		{
			if (Reject == EHCKillReject::InMeetingRoom)
			{
				PC->ClientShowMessage(NSLOCTEXT("HC", "KillMeetingRoom", "In der Kommandozentrale kann niemand getötet werden."));
			}
		}
	}
}

void UHCKillComponent::ServerSetReadyTime(double ServerTime)
{
	KillReadyServerTime = ServerTime;
}

void UHCKillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UHCKillComponent, KillReadyServerTime, COND_OwnerOnly);
}
