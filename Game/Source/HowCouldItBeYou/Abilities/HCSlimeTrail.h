#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HCSlimeTrail.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * What a shapeshift leaves behind (docs/02 §6.4): a short sickly glow and a slime puddle that stays for 60 s.
 * The slime is a few decals with the pictures of Tools/Setup/MakeSlime.py (a flat green disc if they are missing).
 * Created locally on every machine from one multicast, never replicated, no collision.
 * On the biologist's own screen (job, Roles/HCJobs.h) it stays three times as long, and it glows under its UV light.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCSlimeTrail : public AActor
{
	GENERATED_BODY()

public:
	AHCSlimeTrail();

	virtual void Tick(float DeltaSeconds) override;

	static constexpr float LifeSeconds = 60.f;
	/** How long the biologist still sees it. */
	static constexpr float BiologistLifeSeconds = 180.f;
	/** Its UV light makes slime glow up to this far away. */
	static constexpr float UVRange = 1400.f;

protected:
	virtual void BeginPlay() override;

	/** Flat disc, only shown when the slime pictures are missing */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Puddle;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Glow;

	/** One material per slime decal; Fade and Glow are driven from Tick */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SlimeLooks;

	float Age = 0.f;
	/** LifeSeconds, or BiologistLifeSeconds when the local player is the biologist (decided once, at the start). */
	float Life = LifeSeconds;
	/** 0..1: how much the UV light shines on it right now. */
	float UVGlow = 0.f;

	/** The local figure, when it is the biologist (else null). */
	const class AHCCharacter* LocalBiologist() const;
};
