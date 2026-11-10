#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/Actor.h"
#include "HCJumpscareDirector.generated.h"

class AHCPlayerController;
class UCameraComponent;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * The "director" of the 2-3 seconds of horror (docs/02 §7.4). Exists ONLY on the victim's machine, is never
 * replicated: takes the camera, pulls it towards the attacker, shows the true form of the Wandler right in
 * front of the camera, plays the stinger and cuts to black. Witnesses never see any of this.
 *
 * Prototype: the true form is built from basic shapes; a real monster model follows in M11.
 * It always ends by itself (client failsafe), even if the server never answers.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCJumpscareDirector : public AActor
{
	GENERATED_BODY()

public:
	AHCJumpscareDirector();

	/**
	 * Spawns the director locally and starts it.
	 * @param bPreview hc.Jumpscare: only watch, the player stays alive and gets the camera back.
	 */
	static AHCJumpscareDirector* Start(AHCPlayerController* PC, const FVector& AttackerLocation, bool bPreview);

	virtual void Tick(float DeltaSeconds) override;

protected:
	void Begin(AHCPlayerController* InPC, const FVector& InAttackerLocation, bool bInPreview);
	void UpdateCameraAndFace(float DeltaSeconds);
	void CutToBlack();
	void Finish();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> FaceRoot;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Maw;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> Eyes;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> UpperTeeth;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> LowerTeeth;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> FaceLight;

	TWeakObjectPtr<AHCPlayerController> PC;
	TWeakObjectPtr<UCameraComponent> Camera;
	FVector AttackerLocation = FVector::ZeroVector;
	FVector LastCameraLocation = FVector::ZeroVector;
	FRotator StartRotation = FRotator::ZeroRotator;
	FRotator TargetRotation = FRotator::ZeroRotator;
	FRotator CurrentRotation = FRotator::ZeroRotator;
	EHCJumpscareIntensity Intensity = EHCJumpscareIntensity::Full;
	float Elapsed = 0.f;
	float Duration = 2.3f;
	float TurnTime = 0.2f;
	float SavedFOV = 90.f;
	bool bPreview = false;
	bool bStingerPlayed = false;
	bool bFlashed = false;
	bool bBlack = false;
	bool bFinished = false;
	int32 DebugShots = 0;
};
