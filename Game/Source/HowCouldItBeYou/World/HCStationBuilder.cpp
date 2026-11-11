#include "World/HCStationBuilder.h"

#include "Core/HCText.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Components/PostProcessComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCLog.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "TimerManager.h"
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
#include "World/HCModelAssets.h"
#include "Engine/StaticMesh.h"

namespace
{
	constexpr double CellCm = 100.0;
	constexpr float FloorThickness = 20.f;
	constexpr float CeilingThickness = 20.f;
	const FName DecorTag(TEXT("HCDecor"));

	// Expected shapes of the "Modular SciFi" building pieces (cm, pivot at the origin):
	//   floor    X 0..335, Y -8..428, walking surface on top at Z 33
	//   ceiling  X 0..580, Y 0..440, hangs from Z 0 down to -60; beam strips along all four edges, flat in the middle
	//   wall     X -610..0, front looks along -Y, Z 0..390: frames and rails reach out to Y -49, the solid foot (Z 0..90)
	//            to -42, while the large panels between the frames lie at the very back (Y +4)
	//   trim     X -610..0, 10 thick, front looks along -Y, Z 0..87 (ledge at the foot, 40 cm in front of the wall)
	// The wall relief is flattened to half its depth: nobody's camera ends up inside a pipe when hugging a wall.
	//   light    flat lamp 166 cm long along Y, whose front looks along +X
	constexpr double FloorX = 335.0, FloorY = 436.0, FloorYMin = -8.0, FloorTop = 33.0;
	constexpr double CeilX = 580.0, CeilY = 440.0;
	constexpr double WallLen = 610.0, WallHeight = 390.0, WallBack = 4.0, WallFront = 49.25, WallFrame = 44.4, WallSquash = 0.5;
	constexpr double LedgeOffset = 40.0, LedgeDepth = 9.7;

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

double AHCStationBuilder::GetWallFaceDepth() const
{
	const double Half = Layout.WallThickness * 0.5;
	if (!bDressed)
	{
		return Half;
	}
	// Same planes as BuildDressing: the wall's back 1 cm in front of the greybox, then its relief or the ledge, whichever
	// reaches further (~38 cm from the wall line)
	return Half + 1.0 + WallBack * WallSquash + FMath::Max(WallFront, LedgeOffset + LedgeDepth) * WallSquash;
}

void AHCStationBuilder::GetLampGrid(const FHCSpace& Space, int32& OutNX, int32& OutNY) const
{
	if (bDressed)
	{
		OutNX = FMath::Max(1, FMath::RoundToInt32(Space.Rect.Width() * CellCm / CeilX));
		OutNY = FMath::Max(1, FMath::RoundToInt32(Space.Rect.Height() * CellCm / CeilY));
		return;
	}
	OutNX = FMath::Max(1, FMath::RoundToInt32(Space.Rect.Width() / LightSpacingMeters));
	OutNY = FMath::Max(1, FMath::RoundToInt32(Space.Rect.Height() / LightSpacingMeters));
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
	BuildDressing();
	ClearDecor();
	BuildDecor(false);
	ApplyPostProcess();
	if (GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(FlickerTimer, this, &AHCStationBuilder::UpdateFlicker, 0.06f, true);
		StartAmbience();
	}
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

UInstancedStaticMeshComponent* AHCStationBuilder::MakeDressComponent(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetupAttachment(Root);
	Component->SetStaticMesh(Mesh);
	// Purely visual: walking, sight lines and interaction keep using the simple greybox shapes
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->RegisterComponent();
	DressComponents.Add(Component);
	return Component;
}

void AHCStationBuilder::BuildDressing()
{
	const UHCModelSet* Models = HCModels::Get();
	UWorld* World = GetWorld();
	if (!Models || !Models->FloorMesh || !Models->WallMesh || !World || !World->IsGameWorld())
	{
		return; // no environment pack: the greybox is what you see
	}
	bDressed = true;

	// The shapes of the pieces are listed at the top of this file.
	// The kit is dark, glossy metal: it needs far more light than the light grey greybox, and a more open exposure
	CeilingLightCandela *= 4.5f;
	CeilingLightRadius *= 1.25f;
	FixedExposureEV100 -= 1.f;

	UInstancedStaticMeshComponent* Floors = MakeDressComponent(Models->FloorMesh);
	UInstancedStaticMeshComponent* Ceils = MakeDressComponent(Models->CeilingMesh);
	UInstancedStaticMeshComponent* WallFaces = MakeDressComponent(Models->WallMesh);
	UInstancedStaticMeshComponent* Trims = MakeDressComponent(Models->WallTrimMesh);
	UInstancedStaticMeshComponent* Lamps = MakeDressComponent(Models->CeilingLightMesh);
	if (Lamps)
	{
		Lamps->SetCastShadow(false);
	}

	for (const FHCSpace& Space : Layout.Spaces)
	{
		const double X0 = Space.Rect.Min.X * CellCm, Y0 = Space.Rect.Min.Y * CellCm;
		const double W = Space.Rect.Width() * CellCm, H = Space.Rect.Height() * CellCm;
		{
			const int32 NX = FMath::Max(1, FMath::RoundToInt32(W / FloorX)), NY = FMath::Max(1, FMath::RoundToInt32(H / FloorY));
			const double SX = W / (NX * FloorX), SY = H / (NY * FloorY);
			for (int32 j = 0; j < NY; ++j)
			{
				for (int32 i = 0; i < NX; ++i)
				{
					// 1 cm above the greybox floor, so the two never flicker against each other
					Floors->AddInstance(FTransform(FRotator::ZeroRotator,
						FVector(X0 + i * FloorX * SX, Y0 + (j * FloorY - FloorYMin) * SY, 1.0 - FloorTop), FVector(SX, SY, 1.0)));
				}
			}
		}
		if (Ceils)
		{
			const int32 NX = FMath::Max(1, FMath::RoundToInt32(W / CeilX)), NY = FMath::Max(1, FMath::RoundToInt32(H / CeilY));
			const double SX = W / (NX * CeilX), SY = H / (NY * CeilY);
			for (int32 j = 0; j < NY; ++j)
			{
				for (int32 i = 0; i < NX; ++i)
				{
					Ceils->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X0 + i * CeilX * SX, Y0 + j * CeilY * SY, Space.CeilingHeight), FVector(SX, SY, 1.0)));
				}
			}
		}
		if (Lamps)
		{
			// One lamp in the middle of every ceiling piece, so none crosses a beam strip; same grid as the lights of BuildDecor
			int32 NX = 1, NY = 1;
			GetLampGrid(Space, NX, NY);
			for (int32 iy = 0; iy < NY; ++iy)
			{
				for (int32 ix = 0; ix < NX; ++ix)
				{
					const double GX = Space.Rect.Min.X + (ix + 0.5) * Space.Rect.Width() / NX;
					const double GY = Space.Rect.Min.Y + (iy + 0.5) * Space.Rect.Height() / NY;
					Lamps->AddInstance(FTransform(FRotator(-90.f, 0.f, 0.f), FVector(GX * CellCm, GY * CellCm, Space.CeilingHeight - 62.f)));
				}
			}
		}
	}

	// Walls: every solid grid edge has up to two faces, one per neighbouring space. Faces on one line that belong
	// to the same space are merged into runs, and each run is covered with wall modules of roughly their own length.
	struct FFaceKey
	{
		bool bVertical;
		int32 Line;
		int32 Side;  // 0 = the space on the low side (-Y / -X), 1 = the high side
		int32 Space;
		int32 Door;  // door index for the piece above a door, else INDEX_NONE
		bool operator==(const FFaceKey& O) const { return bVertical == O.bVertical && Line == O.Line && Side == O.Side && Space == O.Space && Door == O.Door; }
	};
	TArray<TPair<FFaceKey, int32>> Faces; // key + position along the line
	const FHCStationGrid Grid(Layout);
	TArray<FHCWallEdge> Edges;
	Grid.CollectEdges(Edges);
	TMap<FIntVector, int32> SolidEdges; // (line, position, vertical) of every solid edge -> its door, or INDEX_NONE
	for (const FHCWallEdge& Edge : Edges)
	{
		const int32 Low = Edge.bVertical ? Grid.GetSpaceAt(Edge.Cell.X - 1, Edge.Cell.Y) : Grid.GetSpaceAt(Edge.Cell.X, Edge.Cell.Y - 1);
		const int32 High = Grid.GetSpaceAt(Edge.Cell.X, Edge.Cell.Y);
		const int32 Line = Edge.bVertical ? Edge.Cell.X : Edge.Cell.Y;
		const int32 Position = Edge.bVertical ? Edge.Cell.Y : Edge.Cell.X;
		SolidEdges.Add(FIntVector(Line, Position, Edge.bVertical ? 1 : 0), Edge.DoorIndex);
		if (Low != INDEX_NONE)
		{
			Faces.Add(TPair<FFaceKey, int32>(FFaceKey{ Edge.bVertical, Line, 0, Low, Edge.DoorIndex }, Position));
		}
		if (High != INDEX_NONE)
		{
			Faces.Add(TPair<FFaceKey, int32>(FFaceKey{ Edge.bVertical, Line, 1, High, Edge.DoorIndex }, Position));
		}
	}
	Faces.Sort([](const TPair<FFaceKey, int32>& A, const TPair<FFaceKey, int32>& B)
	{
		const FFaceKey& K = A.Key;
		const FFaceKey& L = B.Key;
		if (K.bVertical != L.bVertical) { return K.bVertical < L.bVertical; }
		if (K.Line != L.Line) { return K.Line < L.Line; }
		if (K.Side != L.Side) { return K.Side < L.Side; }
		if (K.Space != L.Space) { return K.Space < L.Space; }
		if (K.Door != L.Door) { return K.Door < L.Door; }
		return A.Value < B.Value;
	});
	const double Half = Layout.WallThickness * 0.5;
	const double FaceDepth = GetWallFaceDepth();
	// The front of a wall's frames, plus 1 cm: a run that gives way in an inner corner stops here
	const double InnerStop = Half + 1.0 + (WallBack + WallFrame) * WallSquash + 1.0;
	// How far a run reaches past one of its ends, cm (negative: it stops short). Beyond = the first grid step past that end.
	// The pieces are far deeper than the greybox wall, so cut ends would show wherever a run simply stopped at a grid point.
	auto EndReach = [&](const FFaceKey& Key, int32 Beyond, bool bHighEnd) -> double
	{
		const int32 SideCell = Key.Side == 1 ? Key.Line : Key.Line - 1;
		const int32 Next = Key.bVertical ? Grid.GetSpaceAt(SideCell, Beyond) : Grid.GetSpaceAt(Beyond, SideCell);
		if (Next == INDEX_NONE || !Grid.IsOpenBetween(Key.Space, Next))
		{
			// Inner corner: the wall turns towards the space. Two runs reaching into the same corner would sink into each
			// other, frames and ledges crossing, so one of them stops in front of the other's frames. A piece above a door
			// gives way to a full wall; otherwise the run along X gives way and the run along Y goes into the corner.
			const int32 Vertex = bHighEnd ? Beyond : Beyond + 1;
			const int32* Across = SolidEdges.Find(FIntVector(Vertex, SideCell, Key.bVertical ? 0 : 1));
			const bool bAcrossDoor = Across && *Across != INDEX_NONE;
			const bool bThisDoor = Key.Door != INDEX_NONE;
			const bool bGiveWay = bThisDoor != bAcrossDoor ? bThisDoor : !Key.bVertical;
			return bGiveWay ? -InnerStop : 0.0;
		}
		const int32* Wall = SolidEdges.Find(FIntVector(Key.Line, Beyond, Key.bVertical ? 1 : 0));
		if (!Wall)
		{
			// Outer corner: the space wraps around the end of this wall. The run along X ends inside the other wall,
			// the run along Y reaches past the other wall's front, so the corner is closed instead of a notch with two cut ends.
			return Key.bVertical ? FaceDepth + 1.5 : Half + 2.0;
		}
		if (*Wall != INDEX_NONE && Key.Door == INDEX_NONE)
		{
			return Half; // door jamb: the wall reaches the edge of the opening, the door leaves close against it
		}
		if (*Wall == INDEX_NONE && Key.Door != INDEX_NONE)
		{
			return -Half; // piece above a door: only over the opening, between the two jambs
		}
		return 0.0;
	};
	int32 Modules = 0;
	for (int32 Start = 0; Start < Faces.Num();)
	{
		int32 End = Start + 1;
		while (End < Faces.Num() && Faces[End].Key == Faces[Start].Key && Faces[End].Value == Faces[End - 1].Value + 1)
		{
			++End;
		}
		const FFaceKey Key = Faces[Start].Key;
		const int32 First = Faces[Start].Value, Last = Faces[End - 1].Value;
		const double ReachLow = EndReach(Key, First - 1, false), ReachHigh = EndReach(Key, Last + 1, true);
		const double RunStart = First * CellCm - ReachLow;
		const double RunLength = (Last + 1 - First) * CellCm + ReachLow + ReachHigh;
		const double Bottom = Key.Door != INDEX_NONE ? Layout.DoorHeight : 0.0;
		const double Height = Layout.Spaces[Key.Space].CeilingHeight - Bottom;
		Start = End;
		if (Height <= 1.0)
		{
			continue;
		}
		const int32 Count = FMath::Max(1, FMath::RoundToInt32(RunLength / WallLen));
		const double Module = RunLength / Count;
		const bool bHigh = Key.Side == 1;
		// The wall front sits 1 cm in front of the greybox wall; the module runs "backwards" from its pivot
		const double Plane = Key.Line * CellCm + (bHigh ? 1.0 : -1.0) * (Half + 1.0 + WallBack * WallSquash);
		const double TrimPlane = Plane + (bHigh ? 1.0 : -1.0) * LedgeOffset * WallSquash;
		const float WallYaw = Key.bVertical ? (bHigh ? -90.f : 90.f) : (bHigh ? 0.f : 180.f);
		// Which end of the module the pivot sits on follows from the rotation (see the shapes above)
		const bool bWallPivotAtEnd = Key.bVertical ? !bHigh : bHigh;
		for (int32 i = 0; i < Count; ++i)
		{
			const double A = RunStart + i * Module, B = RunStart + (i + 1) * Module;
			// Wall and ledge both look along their own -Y, i.e. they are turned half a circle against WallYaw
			const double Along = bWallPivotAtEnd ? A : B;
			const FVector WallAt = Key.bVertical ? FVector(Plane, Along, Bottom) : FVector(Along, Plane, Bottom);
			WallFaces->AddInstance(FTransform(FRotator(0.f, WallYaw + 180.f, 0.f), WallAt, FVector(Module / WallLen, WallSquash, Height / WallHeight)));
			if (Trims && Key.Door == INDEX_NONE)
			{
				const FVector TrimAt = Key.bVertical ? FVector(TrimPlane, Along, 0.0) : FVector(Along, TrimPlane, 0.0);
				Trims->AddInstance(FTransform(FRotator(0.f, WallYaw + 180.f, 0.f), TrimAt, FVector(Module / WallLen, WallSquash, 1.0)));
			}
			++Modules;
		}
	}

	// The greybox stays underneath as collision and as a dark backing wherever two pieces do not quite meet
	const FLinearColor Backing(0.012f, 0.013f, 0.015f);
	for (UInstancedStaticMeshComponent* Floor : SectorFloors)
	{
		Floor->SetMaterial(0, HCGreybox::MakeColored(this, Backing));
	}
	Walls->SetMaterial(0, HCGreybox::MakeColored(this, Backing));
	Ceilings->SetMaterial(0, HCGreybox::MakeColored(this, Backing));
	LightFixtures->SetVisibility(false);
	UE_LOG(LogHC, Log, TEXT("Station dressed: %d floor pieces, %d ceiling pieces, %d wall modules"),
		Floors->GetInstanceCount(), Ceils ? Ceils->GetInstanceCount() : 0, Modules);
	BuildProps();
}

void AHCStationBuilder::BuildProps()
{
	const UHCModelSet* Models = HCModels::Get();
	if (!Models || Models->Props.Num() == 0)
	{
		return;
	}
	// Every computer builds the same props from the layout alone, so they need no replication. Each solid prop gets
	// an unseen box around it: you cannot walk through a barrel, and the simple shapes keep walking predictable.
	const FHCStationGrid Grid(Layout);
	UInstancedStaticMeshComponent* Blockers = NewObject<UInstancedStaticMeshComponent>(this);
	Blockers->SetMobility(EComponentMobility::Static);
	Blockers->SetupAttachment(Root);
	Blockers->SetStaticMesh(HCGreybox::Cube());
	Blockers->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blockers->SetCanEverAffectNavigation(false);
	Blockers->SetVisibility(false);
	Blockers->RegisterComponent();
	DressComponents.Add(Blockers);

	TMap<FName, UInstancedStaticMeshComponent*> Components;
	int32 Placed = 0;
	// Nothing may reach into the visible walls: the real wall pieces stand far in front of the wall line
	const double WallClear = (GetWallFaceDepth() + 2.0) / CellCm;
	// X, Y in metres of the layout, Z in cm above the floor; Yaw turns the long side (X) of the prop
	auto Put = [&](FName Key, const FVector2D& Where, float Yaw, double Z = 0.0, bool bSolid = true, double Scale = 1.0)
	{
		float OwnYaw = 0.f;
		UStaticMesh* Mesh = Models->FindProp(Key, &OwnYaw);
		if (!Mesh)
		{
			return;
		}
		UInstancedStaticMeshComponent*& Component = Components.FindOrAdd(Key);
		if (!Component)
		{
			Component = MakeDressComponent(Mesh);
		}
		const FRotator Turn(0.f, Yaw + OwnYaw, 0.f);
		// The whole footprint stays inside the visible walls of its space; a prop too close is moved out, not turned
		FVector2D Spot = Where;
		const int32 SpaceIndex = Grid.GetSpaceAt(FMath::FloorToInt32(Where.X), FMath::FloorToInt32(Where.Y));
		if (Layout.Spaces.IsValidIndex(SpaceIndex))
		{
			const FBox Footprint = Mesh->GetBoundingBox().TransformBy(FTransform(Turn, FVector::ZeroVector, FVector(Scale)));
			const FIntRect& R = Layout.Spaces[SpaceIndex].Rect;
			const double MinX = R.Min.X + WallClear - Footprint.Min.X / CellCm, MaxX = R.Max.X - WallClear - Footprint.Max.X / CellCm;
			const double MinY = R.Min.Y + WallClear - Footprint.Min.Y / CellCm, MaxY = R.Max.Y - WallClear - Footprint.Max.Y / CellCm;
			if (MinX <= MaxX)
			{
				Spot.X = FMath::Clamp(Spot.X, MinX, MaxX);
			}
			if (MinY <= MaxY)
			{
				Spot.Y = FMath::Clamp(Spot.Y, MinY, MaxY);
			}
		}
		const FVector At(Spot.X * CellCm, Spot.Y * CellCm, Z);
		Component->AddInstance(FTransform(Turn, At, FVector(Scale)));
		if (bSolid)
		{
			const FBox Shape = Mesh->GetBoundingBox();
			Blockers->AddInstance(FTransform(Turn, At + Turn.RotateVector(Shape.GetCenter() * Scale), Shape.GetSize() * Scale / 100.0));
		}
		++Placed;
	};
	// Is a door or anything you use within Margin metres of the rectangle?
	auto Busy = [&](const FVector2D& A, const FVector2D& B, double Margin)
	{
		const FBox2D Rect(FVector2D(FMath::Min(A.X, B.X) - Margin, FMath::Min(A.Y, B.Y) - Margin), FVector2D(FMath::Max(A.X, B.X) + Margin, FMath::Max(A.Y, B.Y) + Margin));
		for (const FHCTaskStationDef& Def : Layout.TaskStations) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCPropDef& Def : Layout.ChargingStations) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCPropDef& Def : Layout.EmergencyButtons) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCFuseBoxDef& Def : Layout.FuseBoxes) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCDoorDef& Door : Layout.Doors)
		{
			const FVector2D Start(Door.Start.X, Door.Start.Y);
			const FVector2D Along = Door.bVertical ? FVector2D(0.0, Door.Width) : FVector2D(Door.Width, 0.0);
			for (const double T : { 0.0, 0.5, 1.0 })
			{
				if (Rect.IsInside(Start + Along * T)) { return true; }
			}
		}
		return false;
	};
	// A warm standing lamp at a group (cm above the floor); it hangs on the power of its sector
	auto StandLamp = [&](const FVector2D& Where, EHCSector Sector, double Height)
	{
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetupAttachment(Root);
		Lamp->SetRelativeLocation(FVector(Where.X * CellCm, Where.Y * CellCm, Height));
		Lamp->SetIntensityUnits(ELightUnits::Candelas);
		Lamp->SetIntensity(38.f);
		Lamp->SetAttenuationRadius(560.f);
		Lamp->SetLightColor(FLinearColor(1.f, 0.86f, 0.6f));
		Lamp->SetCastShadows(false);
		Lamp->RegisterComponent();
		SectorPropLights[FMath::Clamp(static_cast<int32>(Sector), 0, 3)].Add(Lamp);
	};

	// A layout with props put by hand (the lobby) gets exactly those and nothing generated
	for (const FHCPlacedProp& Prop : Layout.PlacedProps)
	{
		const bool bBollard = Prop.Key == TEXT("Bollard");
		Put(Prop.Key, Prop.Position, Prop.YawDegrees, 0.0, true, bBollard ? 2.4 : 1.0);
		const int32 SpaceIndex = Grid.GetSpaceAt(FMath::FloorToInt32(Prop.Position.X), FMath::FloorToInt32(Prop.Position.Y));
		if (Prop.bLamp && Layout.Spaces.IsValidIndex(SpaceIndex) && Models->FindProp(Prop.Key))
		{
			// The light itself sits a little towards the middle of the room
			const FIntRect& R = Layout.Spaces[SpaceIndex].Rect;
			const FVector2D Middle((R.Min.X + R.Max.X) * 0.5, (R.Min.Y + R.Max.Y) * 0.5);
			StandLamp(Prop.Position + (Middle - Prop.Position).GetSafeNormal() * 0.6, Layout.Spaces[SpaceIndex].Sector, 125.0);
		}
	}

	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num() && Layout.PlacedProps.IsEmpty(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		const FIntRect& R = Space.Rect;
		if (!Space.bCorridor)
		{
			// Rooms: a small group in every corner that is free. Nobody has to pass through a corner.
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const bool bRight = (Corner & 1) != 0, bLow = (Corner & 2) != 0;
				const FVector2D Origin(bRight ? R.Max.X : R.Min.X, bLow ? R.Max.Y : R.Min.Y);
				const double SX = bRight ? -1.0 : 1.0, SY = bLow ? -1.0 : 1.0;
				// U runs along the wall the group leans on (3.4 m), V into the room (2.2 m)
				const bool bAlongX = ((SpaceIndex + Corner) & 1) == 0;
				auto At = [&](double U, double V) { return bAlongX ? Origin + FVector2D(SX * U, SY * V) : Origin + FVector2D(SX * V, SY * U); };
				if (Busy(Origin, At(3.4, 2.2), 1.8))
				{
					continue;
				}
				const float YawU = bAlongX ? (SX > 0.0 ? 0.f : 180.f) : (SY > 0.0 ? 90.f : 270.f);
				// A standing lamp from the light pack in the corner itself: without it nobody would see the group.
				// It hangs on the power of its sector.
				if (Models->FindProp(TEXT("Bollard")))
				{
					// Just clear of the ledges, which meet in the corner
					Put(TEXT("Bollard"), At(0.5, 0.5), YawU, 0.0, true, 2.4);
					StandLamp(At(1.0, 1.0), Space.Sector, 125.0);
				}
				switch ((SpaceIndex * 5 + Corner * 3) % 6)
				{
				case 0:
					Put(TEXT("TankCovered"), At(1.0, 1.0), YawU);
					Put(TEXT("OxygenTank"), At(2.0, 0.6), YawU + 30.f);
					break;
				case 1:
					Put(TEXT("Case"), At(1.15, 0.85), YawU);
					Put(TEXT("Toolbox"), At(1.2, 0.85), YawU + 20.f, 50.0);
					Put(TEXT("Drum"), At(2.35, 0.75), YawU);
					break;
				case 2:
					Put(TEXT("Pallet"), At(1.45, 1.45), YawU);
					Put(TEXT("WireReel"), At(1.45, 1.45), YawU + 35.f, 17.4);
					Put(TEXT("Drum"), At(2.95, 0.7), YawU);
					break;
				case 3:
					Put(TEXT("Drum"), At(0.85, 0.78), YawU);
					Put(TEXT("Drum"), At(1.45, 0.74), YawU + 50.f);
					Put(TEXT("Drum"), At(0.82, 1.4), YawU + 110.f);
					Put(TEXT("Extinguisher"), At(1.95, 0.6), YawU + 90.f);
					break;
				case 4:
				{
					Put(TEXT("WorkLight"), At(1.0, 1.0), YawU + 45.f);
					Put(TEXT("Case"), At(2.4, 0.85), YawU);
					if (Models->FindProp(TEXT("WorkLight")))
					{
						// A battery lamp somebody left on: it keeps burning when the sector loses power
						const FVector2D LampAt = At(1.5, 1.5);
						UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
						Lamp->SetMobility(EComponentMobility::Movable);
						Lamp->SetupAttachment(Root);
						Lamp->SetRelativeLocation(FVector(LampAt.X * CellCm, LampAt.Y * CellCm, 150.0));
						Lamp->SetIntensityUnits(ELightUnits::Candelas);
						Lamp->SetIntensity(45.f);
						Lamp->SetAttenuationRadius(600.f);
						Lamp->SetLightColor(FLinearColor(1.f, 0.82f, 0.55f));
						Lamp->SetCastShadows(false);
						Lamp->RegisterComponent();
					}
					break;
				}
				default:
					Put(TEXT("EnergyNode"), At(0.95, 0.95), YawU);
					Put(TEXT("OxygenTank"), At(1.75, 0.6), YawU);
					Put(TEXT("OxygenTank"), At(2.0, 0.62), YawU + 40.f);
					break;
				}
			}
			continue;
		}
		// Corridors: something small against a wall every few metres, on alternating sides, and loose cables
		const bool bAlongX = R.Width() >= R.Height();
		const int32 Length = bAlongX ? R.Width() : R.Height();
		int32 Count = 0;
		for (int32 Step = 3; Step + 2 < Length; Step += 6, ++Count)
		{
			const bool bHighSide = ((SpaceIndex + Count) & 1) != 0;
			const int32 A = (bAlongX ? R.Min.X : R.Min.Y) + Step;
			const int32 SideCell = bAlongX ? (bHighSide ? R.Max.Y - 1 : R.Min.Y) : (bHighSide ? R.Max.X - 1 : R.Min.X);
			const int32 Behind = SideCell + (bHighSide ? 1 : -1);
			// A real wall behind it, not the mouth of another corridor
			bool bWall = true;
			for (int32 D = -1; D <= 1; ++D)
			{
				const int32 Other = bAlongX ? Grid.GetSpaceAt(A + D, Behind) : Grid.GetSpaceAt(Behind, A + D);
				bWall &= Other == INDEX_NONE || !Grid.IsOpenBetween(SpaceIndex, Other);
			}
			const double WallLine = bHighSide ? SideCell + 1.0 : SideCell;
			const double Out = bHighSide ? -1.0 : 1.0;
			auto At = [&](double FromWall, double Shift)
			{
				return bAlongX ? FVector2D(A + 0.5 + Shift, WallLine + Out * FromWall) : FVector2D(WallLine + Out * FromWall, A + 0.5 + Shift);
			};
			const float YawAlong = bAlongX ? 0.f : 90.f;
			const FVector2D Middle = bAlongX ? FVector2D(A + 2.5, (R.Min.Y + R.Max.Y) * 0.5) : FVector2D((R.Min.X + R.Max.X) * 0.5, A + 2.5);
			if ((Count & 1) == 0 && !Busy(Middle, Middle, 2.2))
			{
				Put(TEXT("Cable"), Middle, YawAlong + (bHighSide ? 180.f : 0.f), 0.6, false);
			}
			if (!bWall || Busy(At(0.55, 0.0), At(0.55, 0.0), 2.6))
			{
				continue;
			}
			switch ((SpaceIndex + Count * 3) % 5)
			{
			case 0:
				Put(TEXT("Extinguisher"), At(0.5, 0.0), YawAlong + 90.f);
				break;
			case 1:
				Put(TEXT("Drum"), At(0.6, 0.0), YawAlong);
				Put(TEXT("Drum"), At(0.62, 0.62), YawAlong + 70.f);
				break;
			case 2:
				Put(TEXT("OxygenTank"), At(0.5, 0.0), YawAlong);
				Put(TEXT("OxygenTank"), At(0.5, 0.3), YawAlong + 60.f);
				break;
			case 3:
				Put(TEXT("Toolbox"), At(0.55, 0.0), YawAlong + 12.f);
				Put(TEXT("RolledMat"), At(0.5, 0.95), YawAlong + 90.f);
				break;
			default:
				Put(TEXT("Case"), At(0.72, 0.0), YawAlong);
				break;
			}
		}
	}
	UE_LOG(LogHC, Log, TEXT("Station props: %d placed, %d of them solid"), Placed, Blockers->GetInstanceCount());
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
	FlickerLights.Reset();
	DecorComponents.Reset();

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SectorLights); ++Index)
	{
		SectorLights[Index].Reset();
		SectorLightCandela[Index].Reset();
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
		int32 NX = 1, NY = 1;
		GetLampGrid(Space, NX, NY);
		// A lamp in every ceiling piece can mean more lamps than the LightSpacingMeters grid: together they give the
		// room as much light as that grid would
		const int32 SpacedLamps = FMath::Max(1, FMath::RoundToInt32(Space.Rect.Width() / LightSpacingMeters))
			* FMath::Max(1, FMath::RoundToInt32(Space.Rect.Height() / LightSpacingMeters));
		const float Share = FMath::Clamp(static_cast<float>(SpacedLamps) / (NX * NY), 0.4f, 1.f);
		for (int32 iy = 0; iy < NY; ++iy)
		{
			for (int32 ix = 0; ix < NX; ++ix)
			{
				const double GX = Space.Rect.Min.X + (ix + 0.5) * Space.Rect.Width() / NX;
				const double GY = Space.Rect.Min.Y + (iy + 0.5) * Space.Rect.Height() / NY;
				const FVector Local(GX * CellCm, GY * CellCm, Space.CeilingHeight - (bDressed ? 85.f : 30.f));

				UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transactional);
				Light->SetMobility(EComponentMobility::Movable); // sectors switch lights off in M7
				Light->SetRelativeLocation(Local);
				Light->SetIntensityUnits(ELightUnits::Candelas);
				Light->SetIntensity(CeilingLightCandela * Share);
				Light->SetAttenuationRadius(CeilingLightRadius);
				Light->SetLightColor(FLinearColor(0.82f, 0.9f, 1.f));
				Light->SetCastShadows(false);
				Light->ComponentTags.Add(*FString::Printf(TEXT("HC.Sector.%s"), SectorName(Space.Sector)));
				Register(Light);
				const int32 SectorSlot = FMath::Clamp(static_cast<int32>(Space.Sector), 0, 3);
				SectorLights[SectorSlot].Add(Light);
				SectorLightCandela[SectorSlot].Add(CeilingLightCandela * Share);
				// Roughly every third lamp is worn out and flickers; which ones follows from their place
				if (FMath::Abs(FMath::RoundToInt32(Local.X / 100.0) * 7 + FMath::RoundToInt32(Local.Y / 100.0) * 5) % 3 == 0)
				{
					FHCFlickerLight Worn;
					Worn.Light = Light;
					Worn.Candela = CeilingLightCandela * Share;
					Worn.Sector = SectorSlot;
					if (UAudioComponent* Buzz = bForConstructionScript ? nullptr : MakeLampBuzz(Local))
					{
						Register(Buzz);
						Worn.Buzz = Buzz;
					}
					FlickerLights.Add(Worn);
				}
				if (Space.Sector != EHCSector::Command)
				{
					// Red emergency light next to every ceiling light, off until the sector loses power
					UPointLightComponent* Red = NewObject<UPointLightComponent>(this, NAME_None, RF_Transactional);
					Red->SetMobility(EComponentMobility::Movable);
					Red->SetRelativeLocation(Local - FVector(0.f, 0.f, 25.f));
					Red->SetIntensityUnits(ELightUnits::Candelas);
					Red->SetIntensity(EmergencyLightCandela * Share);
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
			Label->SetText(HCText::Room(Space.Name));
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
	// Things that stand with their back to a wall are placed against the greybox wall. The real wall pieces stand much
	// further out, so with them each thing moves forward until its back (BackCm behind its centre) is clear of them.
	const double FaceDepth = GetWallFaceDepth();
	auto OffWall = [&](const FVector2D& Position, float Yaw, double BackCm)
	{
		const FVector2D Forward(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
		const bool bAlongX = FMath::Abs(Forward.X) > FMath::Abs(Forward.Y);
		const double Coord = bAlongX ? Position.X : Position.Y;
		const double Ahead = bAlongX ? Forward.X : Forward.Y;
		// Distance from the centre back to the wall line behind it
		const double Gap = Ahead > 0.0 ? Coord - FMath::FloorToDouble(Coord) : FMath::CeilToDouble(Coord) - Coord;
		const double Back = Gap * CellCm - BackCm;
		const double Need = FaceDepth + 1.5;
		return Back < Need ? Position + Forward * ((Need - Back) / CellCm) : Position;
	};

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
		const FVector2D At = OffWall(Prop.Position, Prop.YawDegrees, 20.0);
		const FVector Local(At.X * CellCm, At.Y * CellCm, 70.f);
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
		// Wall box, centre 130 cm above the floor; the switch model reaches 4 cm behind the centre (HCFuseBox)
		const FVector2D At = OffWall(Box.Position, Box.YawDegrees, 4.0);
		const FVector Local(At.X * CellCm, At.Y * CellCm, 130.f);
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
		// Console is 110 cm tall and 60 cm deep, the actor origin is its centre
		const FVector2D At = OffWall(Station.Position, Station.YawDegrees, 30.0);
		const FVector Local(At.X * CellCm, At.Y * CellCm, 55.f);
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

namespace HCAmbience
{
	/** Volume of the ventilation hum (the recording is normalised by hand to a peak of 0.5). */
	constexpr float VentilationVolume = 0.4f;
	/** Volume of a worn lamp's crackle during a burst, and the share of it that stays between bursts. */
	constexpr float BuzzVolume = 0.7f;
	constexpr float BuzzQuietShare = 0.12f;
}

USoundBase* AHCStationBuilder::FindAmbience(FName SoundName) const
{
	// Only in a running game with sound; real recordings come from Tools\SetupSounds.bat (no built-in stand-in)
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || GetNetMode() == NM_DedicatedServer || !World->GetAudioDeviceRaw())
	{
		return nullptr;
	}
	const UHCModelSet* Models = HCModels::Get();
	return Models ? Models->Sounds.FindRef(SoundName).Get() : nullptr;
}

void AHCStationBuilder::StartAmbience()
{
	USoundBase* Hum = Ventilation ? nullptr : FindAmbience(TEXT("Ventilation"));
	if (!Hum)
	{
		return;
	}
	Ventilation = NewObject<UAudioComponent>(this);
	Ventilation->bAutoActivate = false;
	Ventilation->bAllowSpatialization = false; // the hum is everywhere
	Ventilation->SetSound(Hum);
	Ventilation->SetVolumeMultiplier(HCAmbience::VentilationVolume);
	Ventilation->SetupAttachment(GetRootComponent());
	Ventilation->RegisterComponent();
	Ventilation->Play(FMath::FRandRange(0.f, FMath::Max(0.f, Hum->GetDuration() - 1.f)));
	UE_LOG(LogHC, Log, TEXT("Ambience: ventilation %s, %d worn lamps crackle"), *Hum->GetName(),
		FlickerLights.FilterByPredicate([](const FHCFlickerLight& Entry) { return Entry.Buzz.IsValid(); }).Num());
}

UAudioComponent* AHCStationBuilder::MakeLampBuzz(const FVector& Local)
{
	USoundBase* Crackle = FindAmbience(TEXT("LampFlicker"));
	if (!Crackle)
	{
		return nullptr;
	}
	if (!BuzzAttenuation)
	{
		// Audible in the room of the lamp, gone two rooms further
		BuzzAttenuation = NewObject<USoundAttenuation>(this);
		BuzzAttenuation->Attenuation.bAttenuate = true;
		BuzzAttenuation->Attenuation.bSpatialize = true;
		BuzzAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		BuzzAttenuation->Attenuation.AttenuationShapeExtents = FVector(150.f);
		BuzzAttenuation->Attenuation.FalloffDistance = 750.f;
	}
	UAudioComponent* Buzz = NewObject<UAudioComponent>(this);
	Buzz->bAutoActivate = false;
	Buzz->SetSound(Crackle);
	Buzz->AttenuationSettings = BuzzAttenuation;
	Buzz->SetRelativeLocation(Local);
	return Buzz;
}

void AHCStationBuilder::SetBuzz(FHCFlickerLight& Entry, float Level)
{
	UAudioComponent* Buzz = Entry.Buzz.Get();
	if (!Buzz || Level == Entry.BuzzLevel)
	{
		return;
	}
	Entry.BuzzLevel = Level;
	Buzz->SetVolumeMultiplier(Level * HCAmbience::BuzzVolume);
	if (!Buzz->IsPlaying())
	{
		// Every lamp somewhere else in the recording, so that two of them never crackle in step
		const float Seconds = Buzz->Sound ? Buzz->Sound->GetDuration() : 0.f;
		Buzz->Play(FMath::FRandRange(0.f, FMath::Max(0.f, Seconds - 0.5f)));
	}
}

void AHCStationBuilder::UpdateFlicker()
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (FHCFlickerLight& Entry : FlickerLights)
	{
		UPointLightComponent* Light = Entry.Light.Get();
		const float Scale = SectorLightScale[Entry.Sector];
		if (!Light || Scale <= 0.01f)
		{
			SetBuzz(Entry, 0.f);
			continue; // a sector without power is dark and silent anyway
		}
		float Level = 1.f;
		bool bBurst = false;
		if (Now >= Entry.BurstEnd)
		{
			// Steady for a while, then the next burst
			Entry.BurstStart = Now + FMath::FRandRange(2.f, 11.f);
			Entry.BurstEnd = Entry.BurstStart + FMath::FRandRange(0.3f, 1.9f);
		}
		else if (Now >= Entry.BurstStart)
		{
			Level = FMath::FRand() < 0.4f ? 0.06f : FMath::FRandRange(0.25f, 1.1f);
			bBurst = true;
		}
		SetBuzz(Entry, bBurst ? 1.f : HCAmbience::BuzzQuietShare);
		if (Level != Entry.Level)
		{
			Entry.Level = Level;
			Light->SetIntensity(Entry.Candela * Scale * Level);
		}
	}
}

void AHCStationBuilder::SetSectorLights(uint8 SectorIndex, float NormalScale, bool bEmergency)
{
	if (SectorIndex >= UE_ARRAY_COUNT(SectorLights))
	{
		return;
	}
	SectorLightScale[SectorIndex] = NormalScale;
	for (int32 Index = 0; Index < SectorLights[SectorIndex].Num(); ++Index)
	{
		if (UPointLightComponent* Light = SectorLights[SectorIndex][Index].Get())
		{
			const float Candela = SectorLightCandela[SectorIndex].IsValidIndex(Index) ? SectorLightCandela[SectorIndex][Index] : CeilingLightCandela;
			Light->SetVisibility(NormalScale > 0.01f);
			Light->SetIntensity(Candela * NormalScale);
		}
	}
	for (const TWeakObjectPtr<UPointLightComponent>& Entry : SectorPropLights[SectorIndex])
	{
		if (UPointLightComponent* Light = Entry.Get())
		{
			Light->SetVisibility(NormalScale > 0.01f);
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
