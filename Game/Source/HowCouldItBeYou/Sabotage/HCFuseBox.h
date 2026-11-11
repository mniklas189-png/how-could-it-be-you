#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCFuseBox.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Fuse box of one power sector (docs/02 §11.2): hold [E] for about 4 s to bring the power back.
 * Every living player may repair, the Wandler too - otherwise a repair would prove someone is crew (docs/01 §7).
 * Prototype: one hold instead of the planned three switches in order.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCFuseBox : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCFuseBox();

	/** Server, before FinishSpawning. */
	void Configure(uint8 InSectorIndex) { SectorIndex = InSectorIndex; }
	uint8 GetSectorIndex() const { return SectorIndex; }

	/** Where a player stands to use it (test helper). */
	FVector GetStandLocation() const;

	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual float GetHoldDuration(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	bool IsSectorPowered() const;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Lamp;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LampMaterial;

	UPROPERTY(Replicated)
	uint8 SectorIndex = 0;
};
