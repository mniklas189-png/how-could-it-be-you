#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HCUneaseComponent.generated.h"

/**
 * Everything the unease value may depend on (docs/02 §15.5): only things the player knows anyway.
 * Deliberately NO role, no distance to the Wandler, no cooldowns, nobody behind walls, no ghosts - otherwise the
 * heartbeat would give the Wandler away. A unit test guards the size of this struct.
 */
struct HOWCOULDITBEYOU_API FHCUneaseInput
{
	/** Power in the sector the player stands in. */
	bool bSectorPowered = true;
	bool bFlashlightOn = false;
	/** Other living figures in line of sight within 15 m - whoever they really are. */
	int32 VisiblePlayers = 0;
};

struct HOWCOULDITBEYOU_API FHCUnease
{
	/** 0 = calm, 1 = alone in the dark without a light. */
	static float Compute(const FHCUneaseInput& In);
};

/**
 * Runs only for the own figure: darker vignette and a quiet heartbeat when the player is alone in the dark.
 * Purely local and cosmetic, nothing is replicated.
 */
UCLASS(ClassGroup = (HC))
class HOWCOULDITBEYOU_API UHCUneaseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHCUneaseComponent();

	float GetUnease() const { return Unease; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float ComputeTarget() const;

	float Unease = 0.f;
	float Target = 0.f;
	float NextSampleTime = 0.f;
	float NextBeatTime = 0.f;
};
