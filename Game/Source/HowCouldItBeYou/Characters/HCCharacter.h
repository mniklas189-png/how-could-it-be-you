#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/Character.h"
#include "HCCharacter.generated.h"

class UAnimSequence;
class UCameraComponent;
class UHCCharacterMovementComponent;
class UHCInteractionComponent;
class UHCKillComponent;
class UHCShapeshiftComponent;
class UHCUneaseComponent;
class UHCModelSet;
class UTextRenderComponent;
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

	UHCShapeshiftComponent* GetShapeshift() const { return ShapeshiftComponent; }

	/** What everyone sees of this figure: suit colour and name badge. A disguise changes exactly this (docs/02 §6.2). */
	const FHCAppearance& GetAppearance() const { return Appearance; }
	void ServerSetAppearance(int32 InIdentity, const FString& InName);

	/** The Wandler in its true form: monster body, faster, tireless - and everybody sees it with its real name. */
	bool IsMonsterForm() const { return bMonsterForm; }
	/** Factor on walking and sprinting speed (1 unless in the true form). */
	float GetSpeedFactor() const { return SpeedFactor; }
	void ServerSetMonsterForm(bool bOn, float InSpeedFactor);

	/** The Q key: opens the shapeshift menu, or ends the disguise. Does nothing unless this player is the Wandler. */
	void Input_Shapeshift();

	/** The M key: station map (for the Wandler also the sabotage menu). */
	void Input_Map();
	void Input_TrueForm();

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

	/** Everybody nearby hears the switch when someone turns the flashlight on or off (the owner already heard it). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFlashlightClick();

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

	UFUNCTION()
	void OnRep_Appearance();

	/** Replaces the greybox figure with the real model if the model packs are installed. */
	void ApplyRealModel();

	void UpdateBattery(float DeltaSeconds);
	void UpdateFlashlightAim();

	/** Puts the flashlight model (Tools\SetupModels.bat) into the own view and into the hand others see. */
	void ApplyFlashlightModel();
	/** Shows or hides the held flashlight, lets it sway and bob in the own view, follows the hand for the others. */
	void UpdateHeldFlashlight(float DeltaSeconds);
	/** The lens glows while the flashlight is on. */
	void UpdateFlashlightLens();

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

	/** Where the own flashlight hangs in the view (attached to the camera); it sways and bobs. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> FlashlightGrip;

	/** The own flashlight, seen only by its owner. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> FlashlightViewModel;

	/** The flashlight the others see in this figure's right hand. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> FlashlightHandModel;

	/** Lens materials of both models (index 0 view, 1 hand). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlashlightLensMaterials;

	/** Model space -> flashlight space (centred, lens towards +X, real size). Only valid with bFlashlightModel. */
	FTransform FlashlightFit;
	/** The front of the lens in model space: the beam starts here. */
	FVector FlashlightLensInModel = FVector::ZeroVector;
	bool bFlashlightModel = false;
	/** The figure has the bones of a hand (real character model). */
	bool bFlashlightHandBones = false;
	bool bFlashlightLensOn = false;
	FRotator FlashlightLastView = FRotator::ZeroRotator;
	FRotator FlashlightSway = FRotator::ZeroRotator;
	float FlashlightBob = 0.f;
	float FlashlightBobPhase = 0.f;
	/** How far (cm) the own flashlight is pulled back because a wall is in the way. */
	float FlashlightRetract = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCInteractionComponent> Interaction;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCKillComponent> KillComponent;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCShapeshiftComponent> ShapeshiftComponent;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCUneaseComponent> UneaseComponent;

	/** Name badges on chest and back; only readable with light, never floating over the head (docs/01 §13). */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UTextRenderComponent> ChestLabel;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UTextRenderComponent> BackLabel;

	/**
	 * Name above the head, always turned towards the viewer. Shows the apparent identity, so a disguise copies it.
	 * It does not glow: the figure's own helmet light makes it readable up close, darkness still hides it from afar.
	 */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UTextRenderComponent> NameTag;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> ChestPlate;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> BackPlate;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance)
	FHCAppearance Appearance;

	UFUNCTION()
	void OnRep_MonsterForm();
	void UpdateMonsterAnimation();

	UPROPERTY(ReplicatedUsing = OnRep_MonsterForm)
	bool bMonsterForm = false;

	UPROPERTY(Replicated)
	float SpeedFactor = 1.f;

	/** What the monster body plays right now, and until when an attack must not be interrupted. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> MonsterAnim;
	float MonsterAnimLockedUntil = 0.f;
	FColor NameTagColor = FColor::White;
	bool bNameTagColorKnown = false;

	/** Keeps the models loaded (also the monster, so the jumpscare never has to load anything). */
	UPROPERTY(Transient)
	TObjectPtr<UHCModelSet> Models;

	/** Jacket and backpack of the real model take the suit colour. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TintMaterials;

	bool bRealModel = false;

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
