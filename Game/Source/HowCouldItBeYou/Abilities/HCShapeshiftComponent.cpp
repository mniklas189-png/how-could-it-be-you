#include "Abilities/HCShapeshiftComponent.h"

#include "Abilities/HCShapeshiftRules.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCMatchRules.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UHCShapeshiftComponent::UHCShapeshiftComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

double UHCShapeshiftComponent::GetServerNow() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.0;
}

float UHCShapeshiftComponent::GetCooldownRemaining() const
{
	return State == EHCShapeshiftState::Ready ? static_cast<float>(FMath::Max(0.0, ReadyServerTime - GetServerNow())) : 0.f;
}

float UHCShapeshiftComponent::GetStateRemaining() const
{
	return StateEndServerTime > 0.0 ? static_cast<float>(FMath::Max(0.0, StateEndServerTime - GetServerNow())) : 0.f;
}

const AHCPlayerState* UHCShapeshiftComponent::FindPlayer(int32 PlayerId) const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	for (const APlayerState* PS : GS ? GS->PlayerArray : TArray<TObjectPtr<APlayerState>>())
	{
		if (PS && PS->GetPlayerId() == PlayerId && !PS->IsInactive())
		{
			return Cast<AHCPlayerState>(PS);
		}
	}
	return nullptr;
}

void UHCShapeshiftComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner()->HasAuthority() || State == EHCShapeshiftState::Ready || StateEndServerTime <= 0.0 || GetServerNow() < StateEndServerTime)
	{
		return;
	}
	switch (State)
	{
	case EHCShapeshiftState::Transforming:
		FinishTransform();
		break;
	case EHCShapeshiftState::Disguised:
		BeginRevert(); // duration is over
		break;
	case EHCShapeshiftState::Reverting:
		FinishRevert();
		break;
	default:
		break;
	}
}

void UHCShapeshiftComponent::PlayEffect() const
{
	if (AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>())
	{
		// Feet of the figure: the trail lies on the floor
		GS->MulticastPlayShapeshiftFX(GetOwner()->GetActorLocation() - FVector(0.f, 0.f, 90.f));
	}
}

void UHCShapeshiftComponent::ServerRequestShapeshift_Implementation(int32 TargetPlayerId)
{
	AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	const AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const AHCPlayerState* PS = Character ? Character->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!Character || !GM || !PS)
	{
		return;
	}
	const AHCPlayerState* Target = FindPlayer(TargetPlayerId);
	FHCShapeshiftCheck Check;
	Check.bPhasePlaying = GM->GetPhase() == EHCMatchPhase::Playing;
	Check.bIsMonster = PS->GetRoleInfo().Role == EHCRole::Monster;
	Check.bAlive = PS->IsAlive();
	Check.bKillLocked = Character->IsKillLocked();
	Check.State = State;
	Check.Now = GetServerNow();
	Check.ReadyTime = ReadyServerTime;
	Check.bTargetValid = Target && Target != PS && Target->IsAlive();
	Check.bSameShape = Target && Character->GetAppearance().Identity == Target->GetIdentity();
	const EHCShapeshiftReject Reject = FHCShapeshiftRules::Validate(Check);
	if (Reject != EHCShapeshiftReject::None)
	{
		// Not punished: with ping an honest request can be just invalid (docs/02 §2.6). Only the monster gets a hint.
		UE_LOG(LogHCMatch, Log, TEXT("Shapeshift request of %s rejected: %s"), *PS->GetPlayerName(), FHCShapeshiftRules::ToString(Reject));
		if (AHCPlayerController* PC = Cast<AHCPlayerController>(Character->GetController()))
		{
			if (Reject == EHCShapeshiftReject::OnCooldown)
			{
				PC->ClientShowMessage(NSLOCTEXT("HC", "ShiftCooldown", "Die Verwandlung ist noch nicht bereit."));
			}
			else if (Reject == EHCShapeshiftReject::TargetInvalid || Reject == EHCShapeshiftReject::SameShape)
			{
				PC->ClientShowMessage(NSLOCTEXT("HC", "ShiftBadTarget", "In diese Person kannst du dich gerade nicht verwandeln."));
			}
		}
		return;
	}
	const UHCMatchRules* Rules = GM->GetRules();
	TargetId = TargetPlayerId;
	State = EHCShapeshiftState::Transforming;
	StateEndServerTime = Check.Now + FMath::Max(0.1f, Rules ? Rules->ShapeshiftTransformTime : 1.5f);
	Character->GetCharacterMovement()->StopMovementImmediately();
	UE_LOG(LogHCMatch, Log, TEXT("Shapeshift: %s starts turning into %s"), *PS->GetPlayerName(), *Target->GetPlayerName());
	PlayEffect();
}

void UHCShapeshiftComponent::FinishTransform()
{
	AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	const AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const AHCPlayerState* Target = FindPlayer(TargetId);
	if (!Character || !GM || !Target || !Target->IsAlive())
	{
		// Target died or left during the transformation: cancelled, the cooldown is not used up (docs/02 §6.6)
		State = EHCShapeshiftState::Ready;
		StateEndServerTime = 0.0;
		TargetId = INDEX_NONE;
		return;
	}
	const UHCMatchRules* Rules = GM->GetRules();
	const float Duration = Rules ? Rules->ShapeshiftDuration : 45.f;
	Character->ServerSetAppearance(Target->GetIdentity(), Target->GetPlayerName());
	State = EHCShapeshiftState::Disguised;
	StateEndServerTime = Duration > 0.f ? GetServerNow() + Duration : 0.0; // 0 = no limit
	DisguiseName = Target->GetPlayerName();
	UE_LOG(LogHCMatch, Log, TEXT("Shapeshift: %s now looks like %s (%.0f s)"), Character->GetPlayerState() ? *Character->GetPlayerState()->GetPlayerName() : TEXT("?"), *DisguiseName, Duration);
}

void UHCShapeshiftComponent::ServerRequestRevert_Implementation()
{
	const AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	const AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const AHCPlayerState* PS = Character ? Character->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!Character || !GM || !PS)
	{
		return;
	}
	FHCShapeshiftCheck Check;
	Check.bPhasePlaying = GM->GetPhase() == EHCMatchPhase::Playing;
	Check.bIsMonster = PS->GetRoleInfo().Role == EHCRole::Monster;
	Check.bAlive = PS->IsAlive();
	Check.bKillLocked = Character->IsKillLocked();
	Check.State = State;
	if (FHCShapeshiftRules::CanRevert(Check))
	{
		BeginRevert();
	}
}

void UHCShapeshiftComponent::BeginRevert()
{
	const AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const UHCMatchRules* Rules = GM ? GM->GetRules() : nullptr;
	State = EHCShapeshiftState::Reverting;
	StateEndServerTime = GetServerNow() + FMath::Max(0.1f, Rules ? Rules->ShapeshiftTransformTime : 1.5f);
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	UE_LOG(LogHCMatch, Log, TEXT("Shapeshift: %s turns back"), OwnerPawn && OwnerPawn->GetPlayerState() ? *OwnerPawn->GetPlayerState()->GetPlayerName() : TEXT("?"));
	PlayEffect();
}

void UHCShapeshiftComponent::RestoreOwnLook()
{
	AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	const AHCPlayerState* PS = Character ? Character->GetPlayerState<AHCPlayerState>() : nullptr;
	if (PS)
	{
		Character->ServerSetAppearance(PS->GetIdentity(), PS->GetPlayerName());
	}
	DisguiseName.Reset();
	TargetId = INDEX_NONE;
	StateEndServerTime = 0.0;
	State = EHCShapeshiftState::Ready;
}

void UHCShapeshiftComponent::FinishRevert()
{
	const AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const UHCMatchRules* Rules = GM ? GM->GetRules() : nullptr;
	RestoreOwnLook();
	// The cooldown starts when the disguise has ended (docs/01 §4.2)
	ReadyServerTime = GetServerNow() + (Rules ? Rules->ShapeshiftCooldown : 30.f);
}

void UHCShapeshiftComponent::ServerEndForMeeting()
{
	if (State != EHCShapeshiftState::Ready)
	{
		RestoreOwnLook();
	}
}

void UHCShapeshiftComponent::ServerNotifyPlayerGone(int32 PlayerId, bool bDisconnected)
{
	if (PlayerId != TargetId || State == EHCShapeshiftState::Ready)
	{
		return;
	}
	const AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const UHCMatchRules* Rules = GM ? GM->GetRules() : nullptr;
	switch (FHCShapeshiftRules::OnTargetGone(State, bDisconnected, Rules && Rules->bRevertWhenTargetDies))
	{
	case FHCShapeshiftRules::ETargetGone::Cancel:
		UE_LOG(LogHCMatch, Log, TEXT("Shapeshift: cancelled, the target is gone"));
		State = EHCShapeshiftState::Ready;
		StateEndServerTime = 0.0;
		TargetId = INDEX_NONE;
		break;
	case FHCShapeshiftRules::ETargetGone::Revert:
		BeginRevert(); // the identity is no longer in the game
		break;
	default:
		break;
	}
}

void UHCShapeshiftComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UHCShapeshiftComponent, State, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UHCShapeshiftComponent, ReadyServerTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UHCShapeshiftComponent, StateEndServerTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UHCShapeshiftComponent, DisguiseName, COND_OwnerOnly);
}
