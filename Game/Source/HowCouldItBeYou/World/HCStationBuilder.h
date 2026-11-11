#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/HCStationLayout.h"
#include "HCStationBuilder.generated.h"

class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UPostProcessComponent;
class USceneComponent;

/**
 * Builds a greybox station from FHCStationLayout: floors, walls with door openings, ceilings,
 * ceiling lights and floor labels. Geometry is deterministic and built locally on every machine
 * (not replicated); doors and charging stations are replicated actors spawned by the server.
 *
 * Place exactly one in the map at the origin (Tools/Setup/CreateMaps.py does that).
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCStationBuilder : public AActor
{
	GENERATED_BODY()

public:
	AHCStationBuilder();

	/** Which layout to build. Only "Kepler7" exists so far. */
	UPROPERTY(EditAnywhere, Category = "HC|Station")
	FName LayoutName = TEXT("Kepler7");

	/** Hide ceilings in the editor to look into the rooms from above. */
	UPROPERTY(EditAnywhere, Category = "HC|Station")
	bool bShowCeilingInEditor = false;

	/** Show room names on the floor. */
	UPROPERTY(EditAnywhere, Category = "HC|Station")
	bool bShowLabels = true;

	/** Ceiling light brightness in candela. */
	UPROPERTY(EditAnywhere, Category = "HC|Lighting", meta = (ClampMin = "0"))
	float CeilingLightCandela = 30.f;

	UPROPERTY(EditAnywhere, Category = "HC|Lighting", meta = (ClampMin = "100"))
	float CeilingLightRadius = 900.f;

	/** Distance between ceiling lights in meters. */
	UPROPERTY(EditAnywhere, Category = "HC|Lighting", meta = (ClampMin = "2"))
	float LightSpacingMeters = 6.f;

	/** Fixed manual exposure (docs/02 §15.2), EV100. Lower = brighter image. */
	UPROPERTY(EditAnywhere, Category = "HC|Lighting")
	float FixedExposureEV100 = 3.f;

	/** 8 spawn points in the spawn room, >= 120 cm apart (docs/04 §2.1). */
	TArray<FTransform> GetSpawnTransforms() const;

	/** Points safe for teleporting (room/corridor centers), used by /unstuck and the Kill-Z rescue. */
	TArray<FVector> GetSafePoints() const;

	/** Box ghosts must stay in: the station plus a few meters around and above it (docs/02 §7.10). */
	FBox GetGhostBounds() const;

	/** Name of the space at a world location, NAME_None outside. */
	FName GetSpaceNameAt(const FVector& WorldLocation) const;

	/** Power sector (EHCSector as a number) at a world location, -1 outside the station. */
	int32 GetSectorIndexAt(const FVector& WorldLocation) const;

	/**
	 * Local switch for the lights of one sector (docs/02 §11.2): the ceiling lights at NormalScale (0 = off),
	 * the red emergency lights on or off. Lights are not replicated; AHCPowerSector calls this on every machine.
	 */
	void SetSectorLights(uint8 SectorIndex, float NormalScale, bool bEmergency);

	/** Brightness of the red emergency lights in candela: ways and silhouettes stay visible. */
	UPROPERTY(EditAnywhere, Category = "HC|Lighting", meta = (ClampMin = "0"))
	float EmergencyLightCandela = 5.f;

	const FHCStationLayout& GetLayout() const { EnsureLayout(); return Layout; }

	void SetExposure(float EV100);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;

protected:
	virtual void BeginPlay() override;

	/**
	 * Fills Layout if still empty. Needed because in cooked builds OnConstruction does not run and the game mode
	 * asks for spawn points before this actor's BeginPlay.
	 */
	void EnsureLayout() const;
	void LoadLayout();
	void BuildGeometry();
	void BuildDecor(bool bForConstructionScript);
	void ClearDecor();
	void SpawnGameplayActors();
	void ApplyPostProcess();

	FVector GridToWorld(double X, double Y, double Z = 0.0) const;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> Root;

	/** One floor component per power sector, so sectors read differently in the greybox. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> SectorFloors;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UInstancedStaticMeshComponent> Walls;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UInstancedStaticMeshComponent> Ceilings;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UInstancedStaticMeshComponent> LightFixtures;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Lights + labels, recreated by BuildDecor. */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<USceneComponent>> DecorComponents;

	mutable FHCStationLayout Layout;
	bool bGameplayActorsSpawned = false;

	TArray<TWeakObjectPtr<UPointLightComponent>> SectorLights[4];
	TArray<TWeakObjectPtr<UPointLightComponent>> SectorEmergencyLights[4];
};
