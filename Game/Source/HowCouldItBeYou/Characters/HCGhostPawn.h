#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HCGhostPawn.generated.h"

class AHCTaskStation;
class UCameraComponent;
class UFloatingPawnMovement;
class UPointLightComponent;
class USphereComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Figure of a dead player (docs/02 §7.10): flies freely, also through walls, but stays inside the map bounds.
 * Invisible for the living: living clients never even receive it (IsNetRelevantFor), and a living host
 * hides the mesh locally. Ghosts may only watch and finish their own tasks.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCGhostPawn : public APawn
{
	GENERATED_BODY()

public:
	AHCGhostPawn();

	/** Server, right after spawning. */
	void SetIdentity(int32 InIdentity);

	UCameraComponent* GetCamera() const { return Camera; }
	bool IsMeshVisibleLocally() const;

	// --- Tasks (local side) ---
	AHCTaskStation* GetFocusedStation() const { return FocusedStation.Get(); }
	FText GetPromptText() const;
	/** 0..1 while holding, else < 0. */
	float GetHoldProgress() const;
	void BeginTaskInteract();
	void EndTaskInteract();

	/** Server: puts the ghost somewhere (test helper and rescue). */
	void ServerTeleport(const FVector& Location);

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Up(const FInputActionValue& Value);
	void Input_Down(const FInputActionValue& Value);

	/** Owner -> server, about 15 times per second: where the ghost is now. The server checks speed and bounds. */
	UFUNCTION(Server, Unreliable)
	void ServerReportLocation(FVector_NetQuantize NewLocation);

	UFUNCTION(Server, Reliable)
	void ServerBeginTask(AHCTaskStation* Station);

	UFUNCTION(Server, Reliable)
	void ServerEndTask();

	UFUNCTION(Client, Reliable)
	void ClientTaskHoldEnded();

	/** The owner flies locally, so a server teleport has to be told to the owner. */
	UFUNCTION(Client, Reliable)
	void ClientTeleport(FVector_NetQuantize Location);

	UFUNCTION()
	void OnRep_Identity();

	FBox GetBounds() const;
	void UpdateFocus();
	void UpdateLocalVisibility();
	bool IsStationInReach(const AHCTaskStation* Station, float Extra) const;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USphereComponent> Sphere;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Lets the owner see a little in the dark; only switched on on the owner's machine. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> OwnLight;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UPROPERTY(ReplicatedUsing = OnRep_Identity)
	int32 Identity = 0;

	// local
	TWeakObjectPtr<AHCTaskStation> FocusedStation;
	float FocusWaitSeconds = 0.f;
	float NextFocusTime = 0.f;
	float NextReportTime = 0.f;
	float NextVisibilityTime = 0.f;
	bool bLocalHolding = false;
	float LocalHoldStart = 0.f;
	float LocalHoldDuration = 0.f;

	// server
	TWeakObjectPtr<AHCTaskStation> ServerHoldStation;
	float ServerHoldEnd = 0.f;
	double LastReportTime = 0.0;
};
