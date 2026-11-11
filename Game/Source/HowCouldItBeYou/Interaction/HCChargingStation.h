#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCChargingStation.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

/** Flashlight charging station L1-L3: hold [E] to fully charge the battery (docs/01 §4.1). */
UCLASS()
class HOWCOULDITBEYOU_API AHCChargingStation : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCChargingStation();

	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual float GetHoldDuration(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Screen;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Glow;
};
