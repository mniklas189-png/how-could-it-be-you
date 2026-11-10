#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HCCharacter.generated.h"

class UCameraComponent;
class UHCCharacterMovementComponent;
class UHCInteractionComponent;
class UHCKillComponent;
class UPointLightComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FInputActionValue;

/**
 * Living player figure, identical for crew and monster (docs/02 §4.2).
 * Greybox body = engine cylinder + head, coloured per player; hidden for the owner.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit AHCCharacter(const FObjectInitializer& ObjectInitializer);

	// --- State ---
	bool IsAlive() const { return bAlive; }

	/** Central lock for all interactions (docs/02 §10.4). Valid on client and server. */
	bool CanAct() const;

	/** Walking is frozen during loading, intro, role reveal and the result screen. */
	bool CanMove() const;

	/** Monster and victim are locked for about 2.5 s during a kill (docs/02 §7.2). */
	bool IsKillLocked() const { return bKillLocked; }
	void ServerSetKillLocked(bool bLocked);
	UHCKillComponent* GetKillComponent() const { return KillComponent; }

	/** Everyone sees the attack of the figure in its CURRENT shape and hears the scream (docs/02 §7.8). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayKillAttack(FVector_NetQuantize VictimLocation);

	/** Local, ghosts only: red marker above the Wandler. */
	bool IsMonsterMarkerVisible() const;

	float GetBatteryFraction() const { return Battery; }
	bool IsFlashlightOn() const { return bFlashlightOn; }
	float GetStaminaFraction() const;
	bool IsSprinting() const;

	UCameraComponent* GetCamera() const { return Camera; }
	UHCInteractionComponent* GetInteraction() const { return Interaction; }
	UHCCharacterMovementComponent* GetHCMovement() const;

	/** Server: suit colour of this player (set by the game mode). */
	void SetBodyColor(const FLinearColor& InColor);

	// --- Server API ---
	void Server_SetBatteryFull();

	UFUNCTION(Server, Reliable)
	void ServerSetFlashlight(bool bOn);

	/** /unstuck (docs/04 §6): teleport to the nearest free safe point. */
	UFUNCTION(Server, Reliable)
	void ServerUnstuck();

	/** Server: teleport to a valid point near the given location (used by unstuck and Kill-Z). */
	bool TeleportToSafePoint(const FVector& Near);

	// --- AActor / APawn ---
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;

protected:
	virtual void BeginPlay() override;

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_SprintStarted();
	void Input_SprintStopped();
	void Input_ToggleFlashlight();
	void Input_InteractStarted();
	void Input_InteractStopped();
	void Input_Kill();
	void UpdateMonsterMarker();
	void UpdateKillAttack(float DeltaSeconds);

	UFUNCTION()
	void OnRep_FlashlightOn();

	UFUNCTION()
	void OnRep_BodyColor();

	void UpdateBattery(float DeltaSeconds);
	void UpdateFlashlightAim();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> VisorMesh;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USpotLightComponent> Flashlight;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> HelmetLight;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCInteractionComponent> Interaction;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCKillComponent> KillComponent;

	/** Never replicated; each ghost's own PC switches it on (docs/02 §7.10). */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> MonsterMarker;

	UPROPERTY(Replicated)
	bool bKillLocked = false;

	float KillAttackTime = -1.f;
	float NextMarkerCheckTime = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_FlashlightOn)
	bool bFlashlightOn = false;

	/** 0..1, only the owner needs it (HUD). */
	UPROPERTY(Replicated)
	float Battery = 1.f;

	UPROPERTY(Replicated)
	bool bAlive = true;

	UPROPERTY(ReplicatedUsing = OnRep_BodyColor)
	FLinearColor BodyColor = FLinearColor(0.8f, 0.8f, 0.8f);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	float LastFlashlightToggleTime = -10.f;
};
