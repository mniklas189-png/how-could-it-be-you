#include "World/HCStationLayout.h"

namespace HCLayout
{
	static FHCSpace Room(const TCHAR* Name, int32 X0, int32 Y0, int32 X1, int32 Y1, EHCSector Sector, float Ceiling = 400.f)
	{
		FHCSpace S;
		S.Name = Name;
		S.Rect = FIntRect(X0, Y0, X1, Y1);
		S.bCorridor = false;
		S.Sector = Sector;
		S.CeilingHeight = Ceiling;
		return S;
	}

	static FHCSpace Corridor(const TCHAR* Name, int32 X0, int32 Y0, int32 X1, int32 Y1, EHCSector Sector)
	{
		FHCSpace S = Room(Name, X0, Y0, X1, Y1, Sector, 300.f);
		S.bCorridor = true;
		return S;
	}

	/** Door on a horizontal edge (between rows Y-1 and Y). */
	static FHCDoorDef DoorH(const TCHAR* Name, int32 X, int32 Y, bool bSabotageable = false, int32 Width = 3)
	{
		FHCDoorDef D;
		D.Name = Name;
		D.Start = FIntPoint(X, Y);
		D.bVertical = false;
		D.Width = Width;
		D.bSabotageable = bSabotageable;
		return D;
	}

	/** Door on a vertical edge (between columns X-1 and X). */
	static FHCDoorDef DoorV(const TCHAR* Name, int32 X, int32 Y, bool bSabotageable = false, int32 Width = 3)
	{
		FHCDoorDef D = DoorH(Name, X, Y, bSabotageable, Width);
		D.bVertical = true;
		return D;
	}

	static FHCTaskStationDef Station(const TCHAR* TaskId, int32 Step, double X, double Y, float Yaw)
	{
		FHCTaskStationDef S;
		S.TaskId = TaskId;
		S.StepIndex = Step;
		S.Position = FVector2D(X, Y);
		S.YawDegrees = Yaw;
		return S;
	}

	static FHCPropDef Prop(const TCHAR* Name, double X, double Y, float Yaw)
	{
		FHCPropDef P;
		P.Name = Name;
		P.Position = FVector2D(X, Y);
		P.YawDegrees = Yaw;
		return P;
	}
}

const FHCSpace* FHCStationLayout::FindSpace(FName Name) const
{
	return Spaces.FindByPredicate([Name](const FHCSpace& S) { return S.Name == Name; });
}

FHCStationLayout FHCStationLayout::MakeKepler7()
{
	using namespace HCLayout;
	using S = EHCSector;

	FHCStationLayout L;
	L.MapName = TEXT("Kepler7");
	L.SizeX = 90;
	L.SizeY = 76;
	L.SpawnRoom = TEXT("Kommandozentrale");

	// --- Rooms (docs/01 §11.1) ---
	L.Spaces.Add(Room(TEXT("Kommandozentrale"), 33, 0, 57, 12, S::Command));
	L.Spaces.Add(Room(TEXT("Quartiere"), 0, 12, 16, 26, S::North));
	L.Spaces.Add(Room(TEXT("Kommunikation"), 74, 12, 90, 26, S::North));
	L.Spaces.Add(Room(TEXT("Hangar"), 0, 32, 16, 46, S::SouthWest, 600.f));
	L.Spaces.Add(Room(TEXT("Lager"), 0, 52, 16, 66, S::SouthWest));
	L.Spaces.Add(Room(TEXT("Medbay"), 74, 32, 90, 46, S::East));
	L.Spaces.Add(Room(TEXT("BioLabor"), 74, 52, 90, 66, S::East));
	L.Spaces.Add(Room(TEXT("Atrium"), 37, 30, 53, 46, S::East, 600.f));
	L.Spaces.Add(Room(TEXT("Sicherheit"), 57, 30, 69, 38, S::East));
	L.Spaces.Add(Room(TEXT("Reaktor"), 33, 66, 57, 76, S::SouthWest, 500.f));

	// --- Corridors (3 m wide, docs/04 §2.1: >= 2 m) ---
	L.Spaces.Add(Corridor(TEXT("GangNord"), 16, 18, 74, 21, S::North));
	L.Spaces.Add(Corridor(TEXT("KommandoGangWest"), 38, 12, 41, 18, S::Command));
	L.Spaces.Add(Corridor(TEXT("KommandoGangOst"), 49, 12, 52, 18, S::Command));
	L.Spaces.Add(Corridor(TEXT("SpeicheNord"), 43, 21, 46, 30, S::North));
	L.Spaces.Add(Corridor(TEXT("GangWestNord"), 6, 26, 9, 32, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("GangWestSued"), 6, 46, 9, 52, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("GangOstNord"), 81, 26, 84, 32, S::East));
	L.Spaces.Add(Corridor(TEXT("GangOstSued"), 81, 46, 84, 52, S::East));
	L.Spaces.Add(Corridor(TEXT("SpeicheWest"), 16, 37, 37, 40, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("SpeicheOst"), 53, 42, 74, 45, S::East));
	L.Spaces.Add(Corridor(TEXT("SicherheitGangWest"), 53, 33, 57, 36, S::East));
	L.Spaces.Add(Corridor(TEXT("SicherheitGangSued"), 61, 38, 64, 42, S::East));
	L.Spaces.Add(Corridor(TEXT("SpeicheSued"), 43, 46, 46, 57, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("GangSued"), 16, 57, 74, 60, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("ReaktorGangWest"), 38, 60, 41, 66, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("ReaktorGangOst"), 49, 60, 52, 66, S::SouthWest));

	// --- Doors (6 sabotageable ones per docs/01 §11.1) ---
	L.Doors.Add(DoorH(TEXT("Kommando_SW"), 38, 12));
	L.Doors.Add(DoorH(TEXT("Kommando_SO"), 49, 12));
	L.Doors.Add(DoorV(TEXT("Quartiere_Ost"), 16, 18));
	L.Doors.Add(DoorH(TEXT("Quartiere_Sued"), 6, 26, true));
	L.Doors.Add(DoorV(TEXT("Kommunikation_West"), 74, 18));
	L.Doors.Add(DoorH(TEXT("Kommunikation_Sued"), 81, 26, true));
	L.Doors.Add(DoorH(TEXT("Hangar_Nord"), 6, 32));
	L.Doors.Add(DoorV(TEXT("Hangar_Ost"), 16, 37));
	L.Doors.Add(DoorH(TEXT("Hangar_Sued"), 6, 46));
	L.Doors.Add(DoorH(TEXT("Lager_Nord"), 6, 52));
	L.Doors.Add(DoorV(TEXT("Lager_Ost"), 16, 57, true));
	L.Doors.Add(DoorH(TEXT("Medbay_Nord"), 81, 32));
	L.Doors.Add(DoorV(TEXT("Medbay_West"), 74, 42));
	L.Doors.Add(DoorH(TEXT("Medbay_Sued"), 81, 46));
	L.Doors.Add(DoorH(TEXT("BioLabor_Nord"), 81, 52, true));
	L.Doors.Add(DoorV(TEXT("BioLabor_West"), 74, 57));
	L.Doors.Add(DoorH(TEXT("Reaktor_West"), 38, 66, true));
	L.Doors.Add(DoorH(TEXT("Reaktor_Ost"), 49, 66));
	L.Doors.Add(DoorH(TEXT("Atrium_Nord"), 43, 30));
	L.Doors.Add(DoorH(TEXT("Atrium_Sued"), 43, 46));
	L.Doors.Add(DoorV(TEXT("Atrium_West"), 37, 37));
	L.Doors.Add(DoorV(TEXT("Atrium_OstSpeiche"), 53, 42));
	L.Doors.Add(DoorV(TEXT("Atrium_OstSicherheit"), 53, 33));
	L.Doors.Add(DoorV(TEXT("Sicherheit_West"), 57, 33));
	L.Doors.Add(DoorH(TEXT("Sicherheit_Sued"), 61, 38, true));

	// --- Charging stations L1-L3 (never in the command center) ---
	L.ChargingStations.Add(Prop(TEXT("L1_Quartiere"), 0.6, 14.0, 0.f));
	L.ChargingStations.Add(Prop(TEXT("L2_Lager"), 0.6, 64.0, 0.f));
	L.ChargingStations.Add(Prop(TEXT("L3_Medbay"), 89.4, 35.0, 180.f));
	// --- Emergency button in the middle of the command center; the 8 seats stand in a 3 m circle around it ---
	L.EmergencyButtons.Add(Prop(TEXT("Notfall_Knopf"), 45.0, 6.0, 0.f));
	// --- Fuse boxes SK-N, SK-O, SK-SW: on corridor walls, reachable from two directions (docs/01 §7 rule 7).
	//     The box is 20 cm deep, so its centre sits 0.22 m from the wall line.
	L.FuseBoxes.Add({ TEXT("SK_Nord"), S::North, FVector2D(30.0, 18.22), 90.f });
	L.FuseBoxes.Add({ TEXT("SK_Ost"), S::East, FVector2D(64.0, 44.78), -90.f });
	L.FuseBoxes.Add({ TEXT("SK_SuedWest"), S::SouthWest, FVector2D(28.0, 57.22), 90.f });

	// --- Task stations of the prototype set (docs/01 §5 and §11.1). Yaw: 0 = faces east (+X), 90 = south (+Y),
	//     180 = west, -90 = north. Consoles are 60 cm deep, so the centre sits 0.4 m from the wall line.
	L.TaskStations.Add(Station(TEXT("Security_RegisterBadge"), 0, 63.0, 30.4, 90.f));   // Sicherheit, north wall
	L.TaskStations.Add(Station(TEXT("Reactor_Valves"), 0, 40.0, 75.6, -90.f));          // Reaktor, south wall west
	L.TaskStations.Add(Station(TEXT("Reactor_Calibrate"), 0, 50.0, 75.6, -90.f));       // Reaktor, south wall east
	L.TaskStations.Add(Station(TEXT("Sample_Analyze"), 0, 0.4, 57.0, 0.f));             // Lager, west wall
	L.TaskStations.Add(Station(TEXT("Sample_Analyze"), 1, 89.6, 59.0, 180.f));          // Bio-Labor, east wall (centrifuge)
	L.TaskStations.Add(Station(TEXT("Sample_Analyze"), 2, 89.6, 41.0, 180.f));          // Medbay, east wall
	L.TaskStations.Add(Station(TEXT("Comms_AlignAntenna"), 0, 82.0, 12.4, 90.f));       // Kommunikation, north wall
	L.TaskStations.Add(Station(TEXT("Comms_TransmitData"), 0, 89.6, 19.0, 180.f));      // Kommunikation, east wall
	L.TaskStations.Add(Station(TEXT("Comms_TransmitData"), 1, 0.4, 39.0, 0.f));         // Hangar, west wall
	L.TaskStations.Add(Station(TEXT("Storage_ScanCargo"), 0, 8.0, 65.6, -90.f));        // Lager, south wall
	L.TaskStations.Add(Station(TEXT("Quarters_AirFilter"), 0, 8.0, 12.4, 90.f));        // Quartiere, north wall
	L.TaskStations.Add(Station(TEXT("BioLab_Cooling"), 0, 82.0, 65.6, -90.f));          // Bio-Labor, south wall

	return L;
}

FHCStationLayout FHCStationLayout::MakeLobby()
{
	using namespace HCLayout;
	FHCStationLayout L;
	L.MapName = TEXT("Lobby");
	L.SizeX = 16;
	L.SizeY = 12;
	L.SpawnRoom = TEXT("Aufenthaltsraum");
	L.Spaces.Add(Room(TEXT("Aufenthaltsraum"), 0, 0, 16, 12, EHCSector::Command));
	return L;
}

FHCStationLayout FHCStationLayout::MakeByName(FName Name)
{
	if (Name == TEXT("Lobby"))
	{
		return MakeLobby();
	}
	return MakeKepler7();
}

// ---------------------------------------------------------------------------

FHCStationGrid::FHCStationGrid(const FHCStationLayout& InLayout)
	: Layout(InLayout)
{
	Cells.Init(INDEX_NONE, FMath::Max(0, Layout.SizeX * Layout.SizeY));

	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		for (int32 Y = Space.Rect.Min.Y; Y < Space.Rect.Max.Y; ++Y)
		{
			for (int32 X = Space.Rect.Min.X; X < Space.Rect.Max.X; ++X)
			{
				if (X < 0 || Y < 0 || X >= Layout.SizeX || Y >= Layout.SizeY)
				{
					BuildProblems.Add(FString::Printf(TEXT("Space %s leaves the map at (%d, %d)"), *Space.Name.ToString(), X, Y));
					continue;
				}
				int32& Cell = Cells[Y * Layout.SizeX + X];
				if (Cell != INDEX_NONE)
				{
					BuildProblems.Add(FString::Printf(TEXT("Spaces %s and %s overlap at (%d, %d)"),
						*Layout.Spaces[Cell].Name.ToString(), *Space.Name.ToString(), X, Y));
					continue;
				}
				Cell = SpaceIndex;
			}
		}
	}

	for (int32 DoorIndex = 0; DoorIndex < Layout.Doors.Num(); ++DoorIndex)
	{
		const FHCDoorDef& Door = Layout.Doors[DoorIndex];
		for (int32 i = 0; i < Door.Width; ++i)
		{
			const int32 X = Door.Start.X + (Door.bVertical ? 0 : i);
			const int32 Y = Door.Start.Y + (Door.bVertical ? i : 0);
			DoorEdges.Add(MakeKey(X, Y, Door.bVertical), DoorIndex);
		}
	}
}

int32 FHCStationGrid::GetSpaceAt(int32 X, int32 Y) const
{
	if (X < 0 || Y < 0 || X >= Layout.SizeX || Y >= Layout.SizeY)
	{
		return INDEX_NONE;
	}
	return Cells[Y * Layout.SizeX + X];
}

bool FHCStationGrid::IsOpenBetween(int32 A, int32 B) const
{
	if (A == INDEX_NONE || B == INDEX_NONE)
	{
		return false;
	}
	return A == B || (Layout.Spaces[A].bCorridor && Layout.Spaces[B].bCorridor);
}

void FHCStationGrid::CollectEdges(TArray<FHCWallEdge>& OutEdges) const
{
	auto Consider = [this, &OutEdges](int32 X, int32 Y, bool bVertical, int32 A, int32 B)
	{
		if (A == INDEX_NONE && B == INDEX_NONE)
		{
			return;
		}
		if (IsOpenBetween(A, B))
		{
			return;
		}
		FHCWallEdge Edge;
		Edge.Cell = FIntPoint(X, Y);
		Edge.bVertical = bVertical;
		const float HA = A != INDEX_NONE ? Layout.Spaces[A].CeilingHeight : 0.f;
		const float HB = B != INDEX_NONE ? Layout.Spaces[B].CeilingHeight : 0.f;
		Edge.Height = FMath::Max(HA, HB);
		if (const int32* DoorIndex = DoorEdges.Find(MakeKey(X, Y, bVertical)))
		{
			if (A != INDEX_NONE && B != INDEX_NONE)
			{
				Edge.DoorIndex = *DoorIndex;
			}
		}
		OutEdges.Add(Edge);
	};

	// Horizontal edges: between (X, Y-1) and (X, Y)
	for (int32 Y = 0; Y <= Layout.SizeY; ++Y)
	{
		for (int32 X = 0; X < Layout.SizeX; ++X)
		{
			Consider(X, Y, false, GetSpaceAt(X, Y - 1), GetSpaceAt(X, Y));
		}
	}
	// Vertical edges: between (X-1, Y) and (X, Y)
	for (int32 X = 0; X <= Layout.SizeX; ++X)
	{
		for (int32 Y = 0; Y < Layout.SizeY; ++Y)
		{
			Consider(X, Y, true, GetSpaceAt(X - 1, Y), GetSpaceAt(X, Y));
		}
	}
}

void FHCStationGrid::GetDoorSpaces(int32 DoorIndex, int32& OutA, int32& OutB) const
{
	OutA = OutB = INDEX_NONE;
	if (!Layout.Doors.IsValidIndex(DoorIndex))
	{
		return;
	}
	const FHCDoorDef& Door = Layout.Doors[DoorIndex];
	if (Door.bVertical)
	{
		OutA = GetSpaceAt(Door.Start.X - 1, Door.Start.Y);
		OutB = GetSpaceAt(Door.Start.X, Door.Start.Y);
	}
	else
	{
		OutA = GetSpaceAt(Door.Start.X, Door.Start.Y - 1);
		OutB = GetSpaceAt(Door.Start.X, Door.Start.Y);
	}
}

bool FHCStationGrid::CanStep(const FIntPoint& P, const FIntPoint& N, bool bSabotageableDoorsOpen) const
{
	const int32 Here = GetSpaceAt(P.X, P.Y);
	const int32 There = GetSpaceAt(N.X, N.Y);
	if (Here == INDEX_NONE || There == INDEX_NONE)
	{
		return false;
	}
	if (IsOpenBetween(Here, There))
	{
		return true;
	}
	// The edge between P and N: vertical edge at x = max(P.X, N.X), or horizontal edge at y = max(P.Y, N.Y)
	const bool bVertical = P.Y == N.Y;
	const FHCEdgeKey Key = bVertical ? MakeKey(FMath::Max(P.X, N.X), P.Y, true) : MakeKey(P.X, FMath::Max(P.Y, N.Y), false);
	if (const int32* DoorIndex = DoorEdges.Find(Key))
	{
		return bSabotageableDoorsOpen || !Layout.Doors[*DoorIndex].bSabotageable;
	}
	return false;
}

TArray<int32> FHCStationGrid::ComputeDistances(const FIntPoint& Start, bool bSabotageableDoorsOpen) const
{
	TArray<int32> Distances;
	Distances.Init(-1, Cells.Num());
	if (GetSpaceAt(Start.X, Start.Y) == INDEX_NONE)
	{
		return Distances;
	}
	TArray<FIntPoint> Queue;
	Queue.Reserve(Cells.Num());
	Queue.Add(Start);
	Distances[Start.Y * Layout.SizeX + Start.X] = 0;
	const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const FIntPoint P = Queue[Head];
		const int32 DP = Distances[P.Y * Layout.SizeX + P.X];
		for (const FIntPoint& D : Dirs)
		{
			const FIntPoint N = P + D;
			if (GetSpaceAt(N.X, N.Y) == INDEX_NONE || Distances[N.Y * Layout.SizeX + N.X] >= 0)
			{
				continue;
			}
			if (CanStep(P, N, bSabotageableDoorsOpen))
			{
				Distances[N.Y * Layout.SizeX + N.X] = DP + 1;
				Queue.Add(N);
			}
		}
	}
	return Distances;
}

int32 FHCStationGrid::ShortestPath(const FIntPoint& From, const FIntPoint& To) const
{
	if (GetSpaceAt(To.X, To.Y) == INDEX_NONE)
	{
		return -1;
	}
	const TArray<int32> Distances = ComputeDistances(From);
	return Distances[To.Y * Layout.SizeX + To.X];
}

FIntPoint FHCStationGrid::GetSpaceCenter(FName SpaceName) const
{
	if (const FHCSpace* Space = Layout.FindSpace(SpaceName))
	{
		return FIntPoint((Space->Rect.Min.X + Space->Rect.Max.X) / 2, (Space->Rect.Min.Y + Space->Rect.Max.Y) / 2);
	}
	return FIntPoint(INDEX_NONE, INDEX_NONE);
}

int32 FHCStationGrid::CountDoorsOfSpace(int32 SpaceIndex) const
{
	int32 Count = 0;
	for (int32 DoorIndex = 0; DoorIndex < Layout.Doors.Num(); ++DoorIndex)
	{
		int32 A, B;
		GetDoorSpaces(DoorIndex, A, B);
		if (A == SpaceIndex || B == SpaceIndex)
		{
			++Count;
		}
	}
	return Count;
}

TArray<FString> FHCStationGrid::Validate() const
{
	TArray<FString> Problems = BuildProblems;

	// Corridor width (docs/04 §2.1)
	for (const FHCSpace& Space : Layout.Spaces)
	{
		const int32 MinSide = FMath::Min(Space.Rect.Width(), Space.Rect.Height());
		if (Space.bCorridor && MinSide * 100 < 200)
		{
			Problems.Add(FString::Printf(TEXT("Corridor %s is narrower than 200 cm"), *Space.Name.ToString()));
		}
		if (Space.CeilingHeight < (Space.bCorridor ? 270.f : 300.f))
		{
			Problems.Add(FString::Printf(TEXT("Ceiling of %s is too low (%.0f cm)"), *Space.Name.ToString(), Space.CeilingHeight));
		}
	}

	// Doors: size and that they really connect two different, closed-off spaces
	if (Layout.DoorHeight < 230.f)
	{
		Problems.Add(FString::Printf(TEXT("Door height %.0f cm < 230 cm"), Layout.DoorHeight));
	}
	for (int32 DoorIndex = 0; DoorIndex < Layout.Doors.Num(); ++DoorIndex)
	{
		const FHCDoorDef& Door = Layout.Doors[DoorIndex];
		if (Door.Width * 100 - Layout.WallThickness < 140.f)
		{
			Problems.Add(FString::Printf(TEXT("Door %s is narrower than 140 cm"), *Door.Name.ToString()));
		}
		for (int32 i = 0; i < Door.Width; ++i)
		{
			const int32 X = Door.Start.X + (Door.bVertical ? 0 : i);
			const int32 Y = Door.Start.Y + (Door.bVertical ? i : 0);
			const int32 A = Door.bVertical ? GetSpaceAt(X - 1, Y) : GetSpaceAt(X, Y - 1);
			const int32 B = GetSpaceAt(X, Y);
			if (A == INDEX_NONE || B == INDEX_NONE || IsOpenBetween(A, B))
			{
				Problems.Add(FString::Printf(TEXT("Door %s cell %d does not sit between two separated spaces"), *Door.Name.ToString(), i));
				break;
			}
		}
		int32 A, B;
		GetDoorSpaces(DoorIndex, A, B);
		const FName CommandRoom = Layout.SpawnRoom;
		if (Door.bSabotageable && ((A != INDEX_NONE && Layout.Spaces[A].Name == CommandRoom) || (B != INDEX_NONE && Layout.Spaces[B].Name == CommandRoom)))
		{
			Problems.Add(FString::Printf(TEXT("Door %s of the meeting room must not be sabotageable"), *Door.Name.ToString()));
		}
	}

	// Every room needs >= 2 exits (a single-room layout like the lobby has none)
	for (int32 SpaceIndex = 0; Layout.Spaces.Num() > 1 && SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
	{
		if (!Layout.Spaces[SpaceIndex].bCorridor && CountDoorsOfSpace(SpaceIndex) < 2)
		{
			Problems.Add(FString::Printf(TEXT("Room %s has fewer than 2 doors"), *Layout.Spaces[SpaceIndex].Name.ToString()));
		}
	}

	// Fuse boxes: exactly one per power sector, in a corridor of that sector
	TMap<uint8, int32> BoxesPerSector;
	for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
	{
		const int32 BoxSpace = GetSpaceAt(FMath::FloorToInt32(Box.Position.X), FMath::FloorToInt32(Box.Position.Y));
		if (BoxSpace == INDEX_NONE || !Layout.Spaces[BoxSpace].bCorridor || Layout.Spaces[BoxSpace].Sector != Box.Sector)
		{
			Problems.Add(FString::Printf(TEXT("Fuse box %s does not hang in a corridor of its own sector"), *Box.Name.ToString()));
		}
		BoxesPerSector.FindOrAdd(static_cast<uint8>(Box.Sector))++;
	}
	for (const FHCSpace& Space : Layout.Spaces)
	{
		if (Layout.FuseBoxes.Num() > 0 && Space.Sector != EHCSector::Command && BoxesPerSector.FindRef(static_cast<uint8>(Space.Sector)) != 1)
		{
			Problems.AddUnique(FString::Printf(TEXT("Power sector %d needs exactly one fuse box"), static_cast<int32>(Space.Sector)));
		}
	}
	for (const FHCPropDef& Prop : Layout.EmergencyButtons)
	{
		const int32 ButtonSpace = GetSpaceAt(FMath::FloorToInt32(Prop.Position.X), FMath::FloorToInt32(Prop.Position.Y));
		if (ButtonSpace == INDEX_NONE || Layout.Spaces[ButtonSpace].Name != Layout.SpawnRoom)
		{
			Problems.Add(FString::Printf(TEXT("Emergency button %s is not in the meeting room"), *Prop.Name.ToString()));
		}
	}
	// Interaction points: task stations and charging stations (docs/04 §2.1)
	{
		struct FPoint
		{
			FString Name;
			FVector2D Position;
			float Yaw;
		};
		TArray<FPoint> Points;
		for (const FHCTaskStationDef& Station : Layout.TaskStations)
		{
			Points.Add({ FString::Printf(TEXT("Task station %s#%d"), *Station.TaskId.ToString(), Station.StepIndex), Station.Position, Station.YawDegrees });
		}
		for (const FHCPropDef& Prop : Layout.ChargingStations)
		{
			Points.Add({ FString::Printf(TEXT("Charging station %s"), *Prop.Name.ToString()), Prop.Position, Prop.YawDegrees });
		}
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const FPoint& P = Points[i];
			const int32 Space = GetSpaceAt(FMath::FloorToInt32(P.Position.X), FMath::FloorToInt32(P.Position.Y));
			if (Space == INDEX_NONE || Layout.Spaces[Space].bCorridor)
			{
				Problems.Add(FString::Printf(TEXT("%s is not inside a room"), *P.Name));
				continue;
			}
			// Standing spot 1.1 m in front must be in the same room (free 120 x 120 cm area)
			const FVector2D Front(FMath::Cos(FMath::DegreesToRadians(P.Yaw)), FMath::Sin(FMath::DegreesToRadians(P.Yaw)));
			const FVector2D Stand = P.Position + Front * 1.1;
			if (GetSpaceAt(FMath::FloorToInt32(Stand.X), FMath::FloorToInt32(Stand.Y)) != Space)
			{
				Problems.Add(FString::Printf(TEXT("%s has no standing spot in its room"), *P.Name));
			}
			// >= 300 cm to every door centre, so nobody working here blocks a door
			for (const FHCDoorDef& Door : Layout.Doors)
			{
				const FVector2D DoorCentre = Door.bVertical ? FVector2D(Door.Start.X, Door.Start.Y + Door.Width * 0.5) : FVector2D(Door.Start.X + Door.Width * 0.5, Door.Start.Y);
				if (FVector2D::Distance(DoorCentre, P.Position) < 3.0)
				{
					Problems.Add(FString::Printf(TEXT("%s is closer than 300 cm to door %s"), *P.Name, *Door.Name.ToString()));
				}
			}
			// >= 250 cm between interaction points
			for (int32 j = i + 1; j < Points.Num(); ++j)
			{
				if (FVector2D::Distance(P.Position, Points[j].Position) < 2.5)
				{
					Problems.Add(FString::Printf(TEXT("%s is closer than 250 cm to %s"), *P.Name, *Points[j].Name));
				}
			}
		}
	}

	// Reachability from the spawn room (flood fill through open edges and doors)
	if (!Layout.FindSpace(Layout.SpawnRoom))
	{
		Problems.Add(TEXT("Spawn room not found"));
		return Problems;
	}
	const FIntPoint SpawnCell = GetSpaceCenter(Layout.SpawnRoom);

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bAllDoorsOpen = Pass == 0;
		const TArray<int32> Distances = ComputeDistances(SpawnCell, bAllDoorsOpen);
		TSet<int32> Reported;
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			if (Cells[i] != INDEX_NONE && Distances[i] < 0 && !Reported.Contains(Cells[i]))
			{
				Reported.Add(Cells[i]);
				Problems.Add(FString::Printf(TEXT("%s is not reachable from the spawn room%s"),
					*Layout.Spaces[Cells[i]].Name.ToString(), bAllDoorsOpen ? TEXT("") : TEXT(" when all sabotageable doors are locked")));
			}
		}
	}

	return Problems;
}
