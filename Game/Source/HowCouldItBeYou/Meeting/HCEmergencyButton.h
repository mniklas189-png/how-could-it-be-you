#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCEmergencyButton.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

/**
 * The emergency button in the meeting room (docs/02 §8.1): a living player calls a meeting with [E].
 * One use per player and match, locked for a moment after every meeting.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCEmergencyButton : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCEmergencyButton();

	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override;

	/** Where a player stands to press it (test helper). */
	FVector GetStandLocation() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Pedestal;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Button;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Glow;
};
