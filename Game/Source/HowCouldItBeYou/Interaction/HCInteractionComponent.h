#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HCInteractionComponent.generated.h"

class AHCCharacter;

/**
 * Finds interactables in front of the camera, shows the hint and runs instant/hold interactions
 * with server validation (docs/02 §10).
 */
UCLASS(ClassGroup = (HC), meta = (BlueprintSpawnableComponent))
class HOWCOULDITBEYOU_API UHCInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHCInteractionComponent();

	/** Local: [E] pressed / released. */
	void BeginInteract();
	void EndInteract();

	/** Local, for the HUD. */
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }
	FText GetPromptText() const;
	bool IsFocusAvailable() const { return bFocusAvailable; }
	/** 0..1 while holding, else < 0. */
	float GetHoldProgress() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UFUNCTION(Server, Reliable)
	void ServerBeginInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerEndInteract();

	/** Server -> owner: hold finished or was cancelled. */
	UFUNCTION(Client, Reliable)
	void ClientInteractEnded(bool bCompleted);

	void UpdateFocus();
	void SetFocus(AActor* NewFocus);
	bool ServerValidate(AActor* Target, FText& OutReason) const;
	void ServerTickHold();
	AHCCharacter* GetCharacter() const;

	// --- local ---
	TWeakObjectPtr<AActor> FocusedActor;
	bool bFocusAvailable = false;
	FText FocusReason;
	float NextTraceTime = 0.f;
	bool bLocalHolding = false;
	float LocalHoldStart = 0.f;
	float LocalHoldDuration = 0.f;

	// --- server ---
	TWeakObjectPtr<AActor> ServerTarget;
	float ServerHoldEnd = 0.f;
	FVector ServerHoldStartLocation = FVector::ZeroVector;
};
