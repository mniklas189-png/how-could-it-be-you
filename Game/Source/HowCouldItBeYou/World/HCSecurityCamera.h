#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HCSecurityCamera.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * A security camera in a corridor (docs/01 §6, Among Us "Security"). Built locally on every machine by
 * AHCStationBuilder from the layout - it only shows pictures, nothing about it is replicated. While anybody watches
 * the monitors (AHCSecurityConsole::GetViewerCount, replicated) its red lamp blinks, so the others know they are
 * seen. Without power in its sector the picture is gone and the lamp is dark.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCSecurityCamera : public AActor
{
	GENERATED_BODY()

public:
	AHCSecurityCamera();

	/** Before FinishSpawning. */
	void Configure(FName InId, const FText& InLabel, uint8 InSector);

	FName GetCameraId() const { return CameraId; }
	const FText& GetLabel() const { return Label; }
	uint8 GetSectorIndex() const { return SectorIndex; }
	/** Picture is live: the camera's sector has power. */
	bool HasSignal() const;

	/** The picture (made on first use). Only filled by CaptureNow. */
	UTextureRenderTarget2D* GetFeed();
	/** Renders one frame into the feed. Callers decide the frame rate (CCTV is choppy anyway). */
	void CaptureNow();
	/** Time of the last frame (world seconds), -1 if never. */
	double GetLastCaptureTime() const { return LastCapture; }

	/** All cameras of the map, sorted by id (K1, K2 ...). */
	static void GetAll(const UWorld* World, TArray<AHCSecurityCamera*>& OutCameras);

protected:
	virtual void BeginPlay() override;

	void UpdateLamp();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> Root;

	/** Turned down by the camera's pitch; the rod stays upright. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> Head;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Rod;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Hood;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Lens;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Lamp;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> LampLight;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Feed;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LampMaterial;

	FName CameraId;
	FText Label;
	uint8 SectorIndex = 0;
	double LastCapture = -1.0;
	bool bLampOn = false;
	FTimerHandle LampTimer;
};
