#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "HCPowerSector.generated.h"

/**
 * Power state of one sector of the station (docs/02 §11.2): North, East, South-West. The command center has its
 * own emergency power and no sector actor. Only the state is replicated; every machine switches its own copy of
 * the lights: 1 s of flicker, then dark with red emergency lights.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCPowerSector : public AInfo
{
	GENERATED_BODY()

public:
	AHCPowerSector();

	/** Server, before FinishSpawning. Index = EHCSector of the station layout. */
	void Configure(uint8 InSectorIndex) { SectorIndex = InSectorIndex; }

	uint8 GetSectorIndex() const { return SectorIndex; }
	bool IsPowered() const { return bPowered; }
	/** Seconds until the emergency power comes back by itself; 0 while powered. */
	float GetSecondsUntilAutoRestore() const;

	void ServerSetPowered(bool bInPowered, double AutoRestoreServerTime);

	/** "Nord", "Ost", "Süd-West". */
	static FText GetDisplayName(uint8 InSectorIndex);
	static AHCPowerSector* Find(const UWorld* World, uint8 InSectorIndex);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Powered();

	void ApplyLights(float NormalScale, bool bEmergency) const;

	UPROPERTY(Replicated)
	uint8 SectorIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Powered)
	bool bPowered = true;

	UPROPERTY(Replicated)
	double RestoreServerTime = 0.0;

	/** Local: seconds of flicker left after the power went off. */
	float FlickerLeft = 0.f;
	bool bAppliedOnce = false;
};
