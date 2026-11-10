#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCTaskStation.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UHCTaskComponent;
struct FHCTaskDefinition;
struct FHCTaskStepDef;

/**
 * Place of one task step (docs/02 §9.1): TaskId + StepIndex. Several players can use it at the same time.
 * It replicates nothing that changes during a round (dormant). The screen colour is a purely local hint:
 * every machine colours it from the own task list (yellow = something to do here, green = done here).
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCTaskStation : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCTaskStation();

	/** Server, before FinishSpawning. */
	void Configure(FName InTaskId, int32 InStepIndex);

	FName GetTaskId() const { return TaskId; }
	int32 GetStepIndex() const { return StepIndex; }
	const FHCTaskDefinition* GetDefinition() const;
	const FHCTaskStepDef* GetStepDef() const;

	/** Where a player stands to use the station (110 cm in front of it, capsule centre height). */
	FVector GetStandLocation() const;

	// IHCInteractable
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual float GetHoldDuration(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Task();

	void UpdateLocalIndicator();
	static UHCTaskComponent* GetTasksOf(const AHCCharacter* Who);

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Screen;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UTextRenderComponent> Label;

	/** Local hint light in the colour of the screen (never replicated). */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Task, Category = "HC")
	FName TaskId;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Task, Category = "HC")
	int32 StepIndex = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;

	FTimerHandle IndicatorTimer;
	int32 LastIndicatorState = -1;
};
