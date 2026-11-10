#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HCSlimeTrail.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

/**
 * What a shapeshift leaves behind (docs/02 §6.4): a short sickly glow and a slime puddle that stays for 60 s.
 * Created locally on every machine from one multicast, never replicated, no collision.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCSlimeTrail : public AActor
{
	GENERATED_BODY()

public:
	AHCSlimeTrail();

	virtual void Tick(float DeltaSeconds) override;

	static constexpr float LifeSeconds = 60.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Puddle;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Glow;

	float Age = 0.f;
};
