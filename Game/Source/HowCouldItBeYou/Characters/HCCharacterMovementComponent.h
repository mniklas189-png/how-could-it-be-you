#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HCCharacterMovementComponent.generated.h"

/**
 * Walking/sprinting with client-side prediction (docs/02 §4.2).
 * Sprint wish travels in the saved move flags, so client and server agree; stamina is simulated on both.
 * No jumping, no crouching.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FHCSavedMove;

public:
	UHCCharacterMovementComponent();

	void SetWantsToSprint(bool bWants) { bWantsToSprint = bWants; }
	bool WantsToSprint() const { return bWantsToSprint; }

	/** Actually sprinting right now (wish + moving + stamina left). */
	bool IsSprinting() const;

	/** 0..1 */
	float GetStamina() const { return Stamina; }

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void BeginPlay() override;

protected:
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

private:
	bool bWantsToSprint = false;

	/** After running empty, sprinting is blocked until stamina is back above this value. */
	bool bExhausted = false;

	float Stamina = 1.f;
};
