#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/HCTypes.h"
#include "Interaction/HCInteractable.h"
#include "HCCrisisPanel.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * One of the two wall panels of a crisis, the sabotage with a time limit (Sabotage/HCSabotageManager): for a meltdown
 * the two in the reactor, for an oxygen failure one each in two rooms far apart. The crisis is over only when both
 * are held at the same moment, for two seconds; until then the alarm runs, and with it the clock. Built from the
 * layout (FHCCrisisPanelDef), replicated so that everybody sees the warning light.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCCrisisPanel : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCCrisisPanel();

	/** Before FinishSpawning. */
	void Configure(EHCCrisis InCrisis, uint8 InIndex);

	EHCCrisis GetCrisis() const { return Crisis; }
	uint8 GetPanelIndex() const { return PanelIndex; }

	/** Server: does somebody hold it right now? */
	bool IsHeld() const;

	/** "Kernschmelze" / "Sauerstoffausfall". */
	static FText CrisisName(EHCCrisis InCrisis);
	/** What has to be done, one line for the banner and the alarm. */
	static FText CrisisTask(EHCCrisis InCrisis);
	/** "Notkühlung A" ... */
	FText GetPanelName() const;

	// IHCInteractable
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	/** Held until the crisis is over (or let go): longer than any crisis lasts. */
	virtual float GetHoldDuration(const AHCCharacter* Who) const override { return 120.f; }
	virtual void ExecuteInteraction(AHCCharacter* Who) override {}
	virtual void OnHoldChanged(AHCCharacter* Who, bool bHolding) override;
	virtual float GetHoldProgress(const AHCCharacter* Who) const override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override { return Block; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	/** Is its crisis running? */
	bool IsActive() const;

	UPROPERTY(Replicated)
	EHCCrisis Crisis = EHCCrisis::None;

	UPROPERTY(Replicated)
	uint8 PanelIndex = 0;

	/** The panel on the wall (80 x 20 x 100 cm), collision and interaction target. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Block;

	/** The big lever in front of it. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Handle;

	/** Red warning light, flashing during its crisis. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> Alarm;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UTextRenderComponent> Label;

	/** Server: who is holding it (a hold ends by letting go, by death, or when the crisis is over). */
	TArray<TWeakObjectPtr<AHCCharacter>> Holders;
};
