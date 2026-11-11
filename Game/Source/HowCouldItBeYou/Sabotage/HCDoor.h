#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCDoor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EHCDoorState : uint8
{
	Open,
	Closed,
	Locked
};

/**
 * Automatic sliding door (docs/02 §11.3). The panel slides up into the ceiling.
 * - Opens for living players in the trigger, never closes on a player (the frame is inside the trigger).
 * - Panel only blocks pawns when fully closed.
 * - State is replicated, movement is interpolated locally on every machine.
 * - Dormant; every state change flushes dormancy.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCDoor : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCDoor();

	/** Called by the station builder before FinishSpawning. Width/height of the opening in cm. */
	void Configure(float InWidth, float InHeight, float InWallThickness, bool bInSabotageable, FName InDoorId);

	UPROPERTY(EditAnywhere, Replicated, Category = "HC")
	bool bSabotageable = false;

	UPROPERTY(VisibleAnywhere, Replicated, Category = "HC")
	FName DoorId;

	EHCDoorState GetDoorState() const { return DoorState; }

	/** 0 = closed, 1 = open (local, interpolated). */
	float GetOpenAlpha() const { return OpenAlpha; }

	// --- Sabotage (docs/02 §11.3) ---
	/** Locks the door for at most Seconds as soon as nobody stands in the frame; gives up after 2 s. */
	void ServerRequestLock(float Seconds);
	/** Ends a lock or a pending lock at once (time over, meeting, emergency release). */
	void ServerUnlock();
	bool IsLockPending() const { return bLockPending; }
	/** "Quartiere Sued" from the id "Quartiere_Sued". */
	FText GetDisplayName() const;

	// IHCInteractable: emergency release of a locked door, hold for 3 s
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual float GetHoldDuration(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UFUNCTION()
	void OnRep_DoorState();

	void ServerEvaluate();
	void SetDoorState(EHCDoorState NewState);
	bool IsAnyLivingPlayerInTrigger() const;
	void ApplyGeometry();
	void UpdatePanel();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Panel;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> StatusLightA;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> StatusLightB;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(ReplicatedUsing = OnRep_DoorState)
	EHCDoorState DoorState = EHCDoorState::Closed;

	UPROPERTY(EditAnywhere, Replicated, Category = "HC")
	float OpeningWidth = 300.f;

	UPROPERTY(EditAnywhere, Replicated, Category = "HC")
	float OpeningHeight = 240.f;

	UPROPERTY(EditAnywhere, Replicated, Category = "HC")
	float WallThickness = 20.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StatusMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PanelMaterial;

	float OpenAlpha = 0.f;

	/** The two halves you see (door leaves from the model packs, or grey slabs) and where each one is when closed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LeafParts;
	TArray<FVector> LeafHomes;
	TArray<float> LeafSides;

	/** For the door sound: the last state somebody could hear. */
	EHCDoorState HeardState = EHCDoorState::Closed;
	bool bSoundReady = false;
	float LastOccupiedTime = -1000.f;
	/** A living player stands in the frame itself (not just near the door). */
	bool IsFrameOccupied() const;
	bool bLockPending = false;
	float LockRequestedAt = 0.f;
	float LockSeconds = 10.f;
	float LockEndTime = 0.f;
	FTimerHandle EvaluateTimer;
};
