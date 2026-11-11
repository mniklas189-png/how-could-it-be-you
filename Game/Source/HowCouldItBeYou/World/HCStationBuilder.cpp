#include "World/HCStationBuilder.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCLog.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/HCChargingStation.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Meeting/HCEmergencyButton.h"
#include "Sabotage/HCDoor.h"
#include "Sabotage/HCFuseBox.h"
#include "Sabotage/HCPowerSector.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "Tasks/HCTaskStation.h"
#include "World/HCGreyboxAssets.h"

namespace
{
	constexpr double CellCm = 100.0;
	constexpr float FloorThickness = 20.f;
	constexpr float CeilingThickness = 20.f;
	const FName DecorTag(TEXT("HCDecor"));

	FLinearColor SectorFloorColor(EHCSector Sector)
	{
		switch (Sector)
		{
		case EHCSector::Command:   return FLinearColor(0.16f, 0.18f, 0.22f);
		case EHCSector::North:     return FLinearColor(0.12f, 0.20f, 0.20f);
		case EHCSector::East:      return FLinearColor(0.18f, 0.15f, 0.22f);
		case EHCSector::SouthWest: return FLinearColor(0.22f, 0.17f, 0.12f);
		}
		return FLinearColor::Gray;
	}

	const TCHAR* SectorName(EHCSector Sector)
	{
		switch (Sector)
		{
		case EHCSector::Command:   return TEXT("Command");
		case EHCSector::North:     return TEXT("North");
		case EHCSector::East:      return TEXT("East");
		case EHCSector::SouthWest: return TEXT("SouthWest");
		}
		return TEXT("None");
	}
}

AHCStationBuilder::AHCStationBuilder()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Static);

	auto MakeISM = [this](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* ISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		ISM->SetupAttachment(Root);
		ISM->SetStaticMesh(HCGreybox::Cube());
		ISM->SetMobility(EComponentMobility::Static);
		ISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		return ISM;
	};

	for (int32 i = 0; i < 4; ++i)
	{
		SectorFloors.Add(MakeISM(*FString::Printf(TEXT("Floor_%s"), SectorName(static_cast<EHCSector>(i)))));
	}
	Walls = MakeISM(TEXT("Walls"));
	Ceilings = MakeISM(TEXT("Ceilings"));
	LightFixtures = MakeISM(TEXT("LightFixtures"));
	LightFixtures->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LightFixtures->SetCastShadow(false);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
}

void AHCStationBuilder::EnsureLayout() const
{
	if (Layout.Spaces.IsEmpty())
	{
		const_cast<AHCStationBuilder*>(this)->LoadLayout();
	}
}

void AHCStationBuilder::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	LoadLayout();
}

void AHCStationBuilder::LoadLayout()
{
	// Kepler7 and the lobby room; Europa follows in M14.
	Layout = FHCStationLayout::MakeByName(LayoutName);
	if (LayoutName != Layout.MapName)
	{
		UE_LOG(LogHC, Warning, TEXT("Unknown station layout '%s', using Kepler7"), *LayoutName.ToString());
	}
}

FVector AHCStationBuilder::GridToWorld(double X, double Y, double Z) const
{
	return GetActorTransform().TransformPosition(FVector(X * CellCm, Y * CellCm, Z));
}

void AHCStationBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	LoadLayout();
	BuildGeometry();
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		BuildDecor(/*bForConstructionScript*/ true);
	}
}

void AHCStationBuilder::BeginPlay()
{
	Super::BeginPlay();

	LoadLayout();
	BuildGeometry();
	ClearDecor();
	BuildDecor(false);
	ApplyPostProcess();
	// Lights were just rebuilt: bring them in line with sectors that already exist (late join, rebuild)
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		SetSectorLights(It->GetSectorIndex(), It->IsPowered() ? 1.f : 0.f, !It->IsPowered());
	}
	// Thin haze so that flashlights and emergency lights get a visible cone (docs/02 §15.3); local on every machine
	if (GetNetMode() != NM_DedicatedServer && Layout.Spaces.Num() > 1)
	{
		if (AExponentialHeightFog* Fog = GetWorld()->SpawnActor<AExponentialHeightFog>(GetActorLocation(), FRotator::ZeroRotator))
		{
			UExponentialHeightFogComponent* FogComponent = Fog->GetComponent();
			FogComponent->SetFogDensity(0.012f);
			FogComponent->SetFogHeightFalloff(0.02f);
			FogComponent->SetFogInscatteringColor(FLinearColor(0.012f, 0.016f, 0.022f));
			FogComponent->SetVolumetricFog(true);
			FogComponent->SetVolumetricFogScatteringDistribution(0.5f);
			FogComponent->SetVolumetricFogExtinctionScale(1.f);
			FogComponent->SetVolumetricFogDistance(4000.f);
		}
	}

	// Kill-Z >= 1000 cm below the lowest floor (docs/04 §2.1); characters rescue themselves (AHCCharacter::FellOutOfWorld)
	if (AWorldSettings* Settings = GetWorld()->GetWorldSettings())
	{
		Settings->KillZ = GetActorLocation().Z - 1000.f;
	}

	if (HasAuthority())
	{
		SpawnGameplayActors();
	}

	const TArray<FString> Problems = FHCStationGrid(Layout).Validate();
	for (const FString& Problem : Problems)
	{
		UE_LOG(LogHC, Error, TEXT("Station layout problem: %s"), *Problem);
	}
	UE_LOG(LogHC, Log, TEXT("Station %s built: %d spaces, %d doors, %d wall instances"),
		*Layout.MapName.ToString(), Layout.Spaces.Num(), Layout.Doors.Num(), Walls->GetInstanceCount());
}

void AHCStationBuilder::BuildGeometry()
{
	for (UInstancedStaticMeshComponent* Floor : SectorFloors)
	{
		Floor->ClearInstances();
	}
	Walls->ClearInstances();
	Ceilings->ClearInstances();
	LightFixtures->ClearInstances();

	UWorld* World = GetWorld();
	const bool bEditorPreview = World && !World->IsGameWorld();
	const bool bBuildCeilings = !bEditorPreview || bShowCeilingInEditor;

	// Floors and ceilings: one box per space (relative transforms, the builder may be moved/rotated)
	for (const FHCSpace& Space : Layout.Spaces)
	{
		const double CX = (Space.Rect.Min.X + Space.Rect.Max.X) * 0.5 * CellCm;
		const double CY = (Space.Rect.Min.Y + Space.Rect.Max.Y) * 0.5 * CellCm;
		const FVector Size(Space.Rect.Width(), Space.Rect.Height(), 0.0);

		SectorFloors[static_cast<int32>(Space.Sector)]->AddInstance(
			FTransform(FRotator::ZeroRotator, FVector(CX, CY, -FloorThickness * 0.5f), FVector(Size.X, Size.Y, FloorThickness / 100.f)));

		if (bBuildCeilings)
		{
			Ceilings->AddInstance(FTransform(FRotator::ZeroRotator, FVector(CX, CY, Space.CeilingHeight + CeilingThickness * 0.5f),
				FVector(Size.X, Size.Y, CeilingThickness / 100.f)));
		}
	}

	// Walls, door lintels and corner posts
	const FHCStationGrid Grid(Layout);
	TArray<FHCWallEdge> Edges;
	Grid.CollectEdges(Edges);

	const float T = Layout.WallThickness;
	TMap<FIntPoint, float> Posts; // grid vertex -> height
	for (const FHCWallEdge& Edge : Edges)
	{
		const bool bDoor = Edge.DoorIndex != INDEX_NONE;
		const float Bottom = bDoor ? Layout.DoorHeight : 0.f;
		const float Height = Edge.Height - Bottom;
		if (Height <= 1.f)
		{
			continue;
		}
		FVector Center, Scale;
		if (Edge.bVertical)
		{
			Center = FVector(Edge.Cell.X * CellCm, (Edge.Cell.Y + 0.5) * CellCm, Bottom + Height * 0.5f);
			Scale = FVector(T / 100.f, 1.0, Height / 100.f);
		}
		else
		{
			Center = FVector((Edge.Cell.X + 0.5) * CellCm, Edge.Cell.Y * CellCm, Bottom + Height * 0.5f);
			Scale = FVector(1.0, T / 100.f, Height / 100.f);
		}
		Walls->AddInstance(FTransform(FRotator::ZeroRotator, Center, Scale));

		if (!bDoor)
		{
			const FIntPoint A = Edge.Cell;
			const FIntPoint B = Edge.bVertical ? Edge.Cell + FIntPoint(0, 1) : Edge.Cell + FIntPoint(1, 0);
			Posts.FindOrAdd(A) = FMath::Max(Posts.FindRef(A), Edge.Height);
			Posts.FindOrAdd(B) = FMath::Max(Posts.FindRef(B), Edge.Height);
		}
	}
	for (const TPair<FIntPoint, float>& Post : Posts)
	{
		Walls->AddInstance(FTransform(FRotator::ZeroRotator,
			FVector(Post.Key.X * CellCm, Post.Key.Y * CellCm, Post.Value * 0.5f),
			FVector(T / 100.f, T / 100.f, Post.Value / 100.f)));
	}

	// Colours
	for (int32 i = 0; i < SectorFloors.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mat = HCGreybox::MakeColored(this, SectorFloorColor(static_cast<EHCSector>(i))))
		{
			SectorFloors[i]->SetMaterial(0, Mat);
		}
	}
	if (UMaterialInstanceDynamic* WallMat = HCGreybox::MakeColored(this, FLinearColor(0.32f, 0.34f, 0.37f)))
	{
		Walls->SetMaterial(0, WallMat);
	}
	if (UMaterialInstanceDynamic* CeilMat = HCGreybox::MakeColored(this, FLinearColor(0.08f, 0.08f, 0.09f)))
	{
		Ceilings->SetMaterial(0, CeilMat);
	}
	if (UMaterialInstanceDynamic* FixtureMat = HCGreybox::MakeColored(this, FLinearColor(0.9f, 0.95f, 1.f)))
	{
		LightFixtures->SetMaterial(0, FixtureMat);
	}
}

void AHCStationBuilder::ClearDecor()
{
	TArray<USceneComponent*> Existing;
	GetComponents<USceneComponent>(Existing);
	for (USceneComponent* Component : Existing)
	{
		if (Component && Component->ComponentHasTag(DecorTag))
		{
			Component->DestroyComponent();
		}
	}
	DecorComponents.Reset();
}

void AHCStationBuilder::BuildDecor(bool bForConstructionScript)
{
	DecorComponents.Reset();

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SectorLights); ++Index)
	{
		SectorLights[Index].Reset();
		SectorEmergencyLights[Index].Reset();
	}
	auto Register = [this, bForConstructionScript](USceneComponent* Component)
	{
		Component->ComponentTags.Add(DecorTag);
		Component->CreationMethod = bForConstructionScript ? EComponentCreationMethod::UserConstructionScript : EComponentCreationMethod::Instance;
		Component->SetupAttachment(Root);
		Component->RegisterComponent();
		if (!bForConstructionScript)
		{
			AddInstanceComponent(Component);
		}
		DecorComponents.Add(Component);
	};

	for (const FHCSpace& Space : Layout.Spaces)
	{
		const int32 NX = FMath::Max(1, FMath::RoundToInt(Space.Rect.Width() / LightSpacingMeters));
		const int32 NY = FMath::Max(1, FMath::RoundToInt(Space.Rect.Height() / LightSpacingMeters));
		for (int32 iy = 0; iy < NY; ++iy)
		{
			for (int32 ix = 0; ix < NX; ++ix)
			{
				const double GX = Space.Rect.Min.X + (ix + 0.5) * Space.Rect.Width() / NX;
				const double GY = Space.Rect.Min.Y + (iy + 0.5) * Space.Rect.Height() / NY;
				const FVector Local(GX * CellCm, GY * CellCm, Space.CeilingHeight - 30.f);

				UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transactional);
				Light->SetMobility(EComponentMobility::Movable); // sectors switch lights off in M7
				Light->SetRelativeLocation(Local);
				Light->SetIntensityUnits(ELightUnits::Candelas);
				Light->SetIntensity(CeilingLightCandela);
				Light->SetAttenuationRadius(CeilingLightRadius);
				Light->SetLightColor(FLinearColor(0.82f, 0.9f, 1.f));
				Light->SetCastShadows(false);
				Light->ComponentTags.Add(*FString::Printf(TEXT("HC.Sector.%s"), SectorName(Space.Sector)));
				Register(Light);
				const int32 SectorSlot = FMath::Clamp(static_cast<int32>(Space.Sector), 0, 3);
				SectorLights[SectorSlot].Add(Light);
				if (Space.Sector != EHCSector::Command)
				{
					// Red emergency light next to every ceiling light, off until the sector loses power
					UPointLightComponent* Red = NewObject<UPointLightComponent>(this, NAME_None, RF_Transactional);
					Red->SetMobility(EComponentMobility::Movable);
					Red->SetRelativeLocation(Local - FVector(0.f, 0.f, 25.f));
					Red->SetIntensityUnits(ELightUnits::Candelas);
					Red->SetIntensity(EmergencyLightCandela);
					Red->SetAttenuationRadius(CeilingLightRadius);
					Red->SetLightColor(FLinearColor(1.f, 0.06f, 0.04f));
					Red->SetCastShadows(false);
					Red->SetVisibility(false);
					Register(Red);
					SectorEmergencyLights[SectorSlot].Add(Red);
				}

				LightFixtures->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Local.X, Local.Y, Space.CeilingHeight - 3.f), FVector(0.6f, 0.6f, 0.05f)));
			}
		}

		if (bShowLabels && !Space.bCorridor)
		{
			UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transactional);
			const double CX = (Space.Rect.Min.X + Space.Rect.Max.X) * 0.5 * CellCm;
			const double CY = (Space.Rect.Min.Y + Space.Rect.Max.Y) * 0.5 * CellCm;
			Label->SetRelativeLocation(FVector(CX, CY, 1.f));
			Label->SetRelativeRotation(FRotator(90.f, 180.f, 0.f)); // lying flat, readable from the south
			Label->SetText(FText::FromName(Space.Name));
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetVerticalAlignment(EVRTA_TextCenter);
			Label->SetWorldSize(60.f);
			Label->SetTextRenderColor(FColor(160, 170, 180));
			Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Register(Label);
		}
	}
}

void AHCStationBuilder::SpawnGameplayActors()
{
	if (bGameplayActorsSpawned)
	{
		return;
	}
	bGameplayActorsSpawned = true;

	UWorld* World = GetWorld();
	const float T = Layout.WallThickness;

	for (const FHCDoorDef& Door : Layout.Doors)
	{
		FVector LocalCenter;
		float Yaw;
		if (Door.bVertical)
		{
			LocalCenter = FVector(Door.Start.X * CellCm, (Door.Start.Y + Door.Width * 0.5) * CellCm, 0.0);
			Yaw = 0.f; // passes along X
		}
		else
		{
			LocalCenter = FVector((Door.Start.X + Door.Width * 0.5) * CellCm, Door.Start.Y * CellCm, 0.0);
			Yaw = 90.f; // passes along Y
		}
		const FTransform DoorTransform(FRotator(0.f, GetActorRotation().Yaw + Yaw, 0.f), GetActorTransform().TransformPosition(LocalCenter));
		AHCDoor* DoorActor = World->SpawnActorDeferred<AHCDoor>(AHCDoor::StaticClass(), DoorTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (DoorActor)
		{
			DoorActor->Configure(Door.Width * CellCm - T, Layout.DoorHeight, T, Door.bSabotageable, Door.Name);
			DoorActor->FinishSpawning(DoorTransform);
		}
	}

	for (const FHCPropDef& Prop : Layout.ChargingStations)
	{
		// Station body is 40 cm deep, stands with its back against the wall, origin at its center
		const FVector Local(Prop.Position.X * CellCm, Prop.Position.Y * CellCm, 70.f);
		const FTransform PropTransform(FRotator(0.f, GetActorRotation().Yaw + Prop.YawDegrees, 0.f), GetActorTransform().TransformPosition(Local));
		World->SpawnActor<AHCChargingStation>(AHCChargingStation::StaticClass(), PropTransform);
	}

	// One power sector per sector with rooms; the command center has its own emergency power.
	// Maps without fuse boxes (lobby) have no outages.
	TSet<uint8> PowerSectors;
	for (const FHCSpace& Space : Layout.Spaces)
	{
		if (Space.Sector != EHCSector::Command && Layout.FuseBoxes.Num() > 0)
		{
			PowerSectors.Add(static_cast<uint8>(Space.Sector));
		}
	}
	for (const uint8 SectorIndex : PowerSectors)
	{
		AHCPowerSector* SectorActor = World->SpawnActorDeferred<AHCPowerSector>(AHCPowerSector::StaticClass(), GetActorTransform(), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (SectorActor)
		{
			SectorActor->Configure(SectorIndex);
			SectorActor->FinishSpawning(GetActorTransform());
		}
	}
	for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
	{
		// Wall box, centre 130 cm above the floor
		const FVector Local(Box.Position.X * CellCm, Box.Position.Y * CellCm, 130.f);
		const FTransform BoxTransform(FRotator(0.f, GetActorRotation().Yaw + Box.YawDegrees, 0.f), GetActorTransform().TransformPosition(Local));
		AHCFuseBox* BoxActor = World->SpawnActorDeferred<AHCFuseBox>(AHCFuseBox::StaticClass(), BoxTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (BoxActor)
		{
			BoxActor->Configure(static_cast<uint8>(Box.Sector));
			BoxActor->FinishSpawning(BoxTransform);
		}
	}
	for (const FHCPropDef& Prop : Layout.EmergencyButtons)
	{
		// Pedestal is 100 cm tall, the actor origin is its centre
		const FVector Local(Prop.Position.X * CellCm, Prop.Position.Y * CellCm, 50.f);
		const FTransform PropTransform(FRotator(0.f, GetActorRotation().Yaw + Prop.YawDegrees, 0.f), GetActorTransform().TransformPosition(Local));
		World->SpawnActor<AHCEmergencyButton>(AHCEmergencyButton::StaticClass(), PropTransform);
	}
	for (const FHCTaskStationDef& Station : Layout.TaskStations)
	{
		// Console is 110 cm tall, the actor origin is its centre
		const FVector Local(Station.Position.X * CellCm, Station.Position.Y * CellCm, 55.f);
		const FTransform StationTransform(FRotator(0.f, GetActorRotation().Yaw + Station.YawDegrees, 0.f), GetActorTransform().TransformPosition(Local));
		AHCTaskStation* StationActor = World->SpawnActorDeferred<AHCTaskStation>(AHCTaskStation::StaticClass(), StationTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (StationActor)
		{
			StationActor->Configure(Station.TaskId, Station.StepIndex);
			StationActor->FinishSpawning(StationTransform);
		}
	}
}

void AHCStationBuilder::ApplyPostProcess()
{
	SetExposure(FixedExposureEV100);
	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.5f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.15f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.4f;
}

void AHCStationBuilder::SetExposure(float EV100)
{
	FixedExposureEV100 = EV100;
	FPostProcessSettings& S = PostProcess->Settings;
	// Fixed exposure: min = max, so the eye never adapts and dark stays dark (docs/02 §15.2)
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMinBrightness = EV100;
	S.AutoExposureMaxBrightness = EV100;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.f;
}

TArray<FTransform> AHCStationBuilder::GetSpawnTransforms() const
{
	EnsureLayout();
	TArray<FTransform> Result;
	const FHCSpace* Room = Layout.FindSpace(Layout.SpawnRoom);
	if (!Room)
	{
		return Result;
	}
	const double CX = (Room->Rect.Min.X + Room->Rect.Max.X) * 0.5;
	const double CY = (Room->Rect.Min.Y + Room->Rect.Max.Y) * 0.5;
	constexpr double Radius = 3.0; // m -> 2.35 m between neighbours
	for (int32 i = 0; i < 8; ++i)
	{
		const double Angle = 2.0 * PI * i / 8.0;
		const FVector Location = GridToWorld(CX + Radius * FMath::Cos(Angle), CY + Radius * FMath::Sin(Angle), 92.0);
		// Look away from the center, towards the walls of the room
		const FRotator Facing(0.f, FMath::RadiansToDegrees(Angle) + GetActorRotation().Yaw, 0.f);
		Result.Add(FTransform(Facing, Location));
	}
	return Result;
}

TArray<FVector> AHCStationBuilder::GetSafePoints() const
{
	EnsureLayout();
	TArray<FVector> Result;
	for (const FHCSpace& Space : Layout.Spaces)
	{
		Result.Add(GridToWorld((Space.Rect.Min.X + Space.Rect.Max.X) * 0.5, (Space.Rect.Min.Y + Space.Rect.Max.Y) * 0.5, 92.0));
	}
	for (const FTransform& Spawn : GetSpawnTransforms())
	{
		Result.Add(Spawn.GetLocation());
	}
	return Result;
}

FBox AHCStationBuilder::GetGhostBounds() const
{
	EnsureLayout();
	float MaxCeiling = 300.f;
	for (const FHCSpace& Space : Layout.Spaces)
	{
		MaxCeiling = FMath::Max(MaxCeiling, Space.CeilingHeight);
	}
	const FVector A = GridToWorld(-3.0, -3.0, 60.0);
	const FVector B = GridToWorld(Layout.SizeX + 3.0, Layout.SizeY + 3.0, MaxCeiling + 250.0);
	return FBox(A.ComponentMin(B), A.ComponentMax(B));
}

int32 AHCStationBuilder::GetSectorIndexAt(const FVector& WorldLocation) const
{
	const FHCSpace* Space = Layout.FindSpace(GetSpaceNameAt(WorldLocation));
	return Space ? static_cast<int32>(Space->Sector) : INDEX_NONE;
}

void AHCStationBuilder::SetSectorLights(uint8 SectorIndex, float NormalScale, bool bEmergency)
{
	if (SectorIndex >= UE_ARRAY_COUNT(SectorLights))
	{
		return;
	}
	for (const TWeakObjectPtr<UPointLightComponent>& Entry : SectorLights[SectorIndex])
	{
		if (UPointLightComponent* Light = Entry.Get())
		{
			Light->SetVisibility(NormalScale > 0.01f);
			Light->SetIntensity(CeilingLightCandela * NormalScale);
		}
	}
	for (const TWeakObjectPtr<UPointLightComponent>& Entry : SectorEmergencyLights[SectorIndex])
	{
		if (UPointLightComponent* Light = Entry.Get())
		{
			Light->SetVisibility(bEmergency);
		}
	}
}

FName AHCStationBuilder::GetSpaceNameAt(const FVector& WorldLocation) const
{
	EnsureLayout();
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const int32 X = FMath::FloorToInt(Local.X / CellCm);
	const int32 Y = FMath::FloorToInt(Local.Y / CellCm);
	const FHCStationGrid Grid(Layout);
	const int32 Index = Grid.GetSpaceAt(X, Y);
	return Index != INDEX_NONE ? Layout.Spaces[Index].Name : NAME_None;
}
