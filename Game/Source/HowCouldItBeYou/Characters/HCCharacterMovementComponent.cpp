#include "Characters/HCCharacterMovementComponent.h"

#include "Characters/HCCharacter.h"
#include "Core/HCGameSettings.h"
#include "GameFramework/Character.h"

/** Saved move carrying the sprint wish (FLAG_Custom_0). */
class FHCSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	uint8 bSavedWantsToSprint : 1 = 0;

	virtual void Clear() override
	{
		Super::Clear();
		bSavedWantsToSprint = 0;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = Super::GetCompressedFlags();
		if (bSavedWantsToSprint)
		{
			Result |= FLAG_Custom_0;
		}
		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		if (bSavedWantsToSprint != static_cast<FHCSavedMove*>(NewMove.Get())->bSavedWantsToSprint)
		{
			return false;
		}
		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
		if (const UHCCharacterMovementComponent* Move = Cast<UHCCharacterMovementComponent>(C->GetCharacterMovement()))
		{
			bSavedWantsToSprint = Move->bWantsToSprint;
		}
	}

	virtual void PrepMoveFor(ACharacter* C) override
	{
		Super::PrepMoveFor(C);
		if (UHCCharacterMovementComponent* Move = Cast<UHCCharacterMovementComponent>(C->GetCharacterMovement()))
		{
			Move->bWantsToSprint = bSavedWantsToSprint;
		}
	}
};

class FHCNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	explicit FHCNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FHCSavedMove());
	}
};

UHCCharacterMovementComponent::UHCCharacterMovementComponent()
{
	MaxWalkSpeed = 300.f;
	MaxStepHeight = 45.f;
	SetWalkableFloorAngle(44.f);
	NavAgentProps.bCanCrouch = false;
	NavAgentProps.bCanJump = false;
	NavAgentProps.AgentRadius = 40.f;
	NavAgentProps.AgentHeight = 180.f;
	BrakingDecelerationWalking = 1600.f;
	GroundFriction = 8.f;
	bOrientRotationToMovement = false;
}

void UHCCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	MaxWalkSpeed = UHCGameSettings::Get()->WalkSpeed;
}

bool UHCCharacterMovementComponent::IsSprinting() const
{
	return bWantsToSprint && !bExhausted && Stamina > 0.f && IsMovingOnGround() && Velocity.SizeSquared2D() > 100.f;
}

float UHCCharacterMovementComponent::GetMaxSpeed() const
{
	// Meetings, the role screen and the kill lock hold on the server too, not only in the input (docs/02 §8.2)
	if (const AHCCharacter* HCOwner = Cast<AHCCharacter>(CharacterOwner); HCOwner && !HCOwner->CanMove())
	{
		return 0.f;
	}
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking)
	{
		const UHCGameSettings* Settings = UHCGameSettings::Get();
		const bool bCanSprint = bWantsToSprint && !bExhausted && Stamina > 0.f;
		return bCanSprint ? Settings->SprintSpeed : Settings->WalkSpeed;
	}
	return Super::GetMaxSpeed();
}

void UHCCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	const UHCGameSettings* Settings = UHCGameSettings::Get();
	if (IsSprinting())
	{
		Stamina = FMath::Max(0.f, Stamina - DeltaSeconds / FMath::Max(0.1f, Settings->StaminaSeconds));
		if (Stamina <= 0.f)
		{
			bExhausted = true;
		}
	}
	else
	{
		Stamina = FMath::Min(1.f, Stamina + DeltaSeconds / FMath::Max(0.1f, Settings->StaminaRecoverySeconds));
		if (bExhausted && Stamina >= 0.3f)
		{
			bExhausted = false;
		}
	}
}

void UHCCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UHCCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UHCCharacterMovementComponent* MutableThis = const_cast<UHCCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FHCNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}
