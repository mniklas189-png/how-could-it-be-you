#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HCKillComponent.generated.h"

class AHCCharacter;

/**
 * Killing (docs/02 §7.1). Every character has this component, crew included - otherwise a cheater could
 * tell the monster by the build of a figure. It is only ever active for the monster:
 * the owner looks for a target locally, the server decides (AHCGameMode::RequestKill).
 */
UCLASS(ClassGroup = (HC))
class HOWCOULDITBEYOU_API UHCKillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHCKillComponent();

	/** Local, monster only: living player in the crosshair within kill range, or null. */
	AHCCharacter* GetLocalTarget() const { return LocalTarget.Get(); }

	/** Seconds until the next kill is allowed (owner and server know the real value). */
	float GetCooldownRemaining() const;

	/** Local input (left mouse button). Without a valid target nothing visible or audible happens. */
	void TryKill();

	UFUNCTION(Server, Reliable)
	void ServerRequestKill(AHCCharacter* Target);

	// --- Server ---
	void ServerSetReadyTime(double ServerTime);
	double GetReadyTime() const { return KillReadyServerTime; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	bool IsOwnerLocalMonster() const;
	void UpdateLocalTarget();

	/** Only the owner gets it; for everyone else (and for crew) it stays 0. */
	UPROPERTY(Replicated)
	double KillReadyServerTime = 0.0;

	TWeakObjectPtr<AHCCharacter> LocalTarget;
	float NextSearchTime = 0.f;
	double LastRequestTime = -10.0;
};
