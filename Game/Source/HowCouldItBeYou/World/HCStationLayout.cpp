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
	L.TaskStations.Add(Station(TEXT("Hangar_RepairWiring"), 0, 13.0, 45.6, -90.f));     // Hangar, south wall east
	L.TaskStations.Add(Station(TEXT("Atrium_Lights"), 0, 39.0, 30.4, 90.f));            // Atrium, north wall west
	L.TaskStations.Add(Station(TEXT("Medbay_MemoryTest"), 0, 77.0, 32.4, 90.f));        // Medbay, north wall west
	L.TaskStations.Add(Station(TEXT("Power_Reroute"), 0, 13.0, 25.6, -90.f));           // Quartiere, south wall east
	L.TaskStations.Add(Station(TEXT("Power_Reroute"), 1, 68.6, 34.0, 180.f));           // Sicherheit, east wall

	return L;
}

FHCStationLayout FHCStationLayout::MakeIkarus()
{
	using namespace HCLayout;
	using S = EHCSector;

	FHCStationLayout L;
	L.MapName = TEXT("Ikarus");
	L.SizeX = 99;
	L.SizeY = 60;
	L.SpawnRoom = TEXT("Bruecke");
	// Every room is furnished by hand below, so no two look alike; only the corridors get the generated odds and ends
	L.bAutoRoomProps = false;

	// A room of several parts, holes cut out of it, light colour and brightness
	auto AddRoom = [&L](const TCHAR* Name, S Sector, float Ceiling, std::initializer_list<FIntRect> Parts, const FLinearColor& Light,
		float LightScale = 1.f, std::initializer_list<FIntRect> Holes = {})
	{
		const FIntRect* First = Parts.begin();
		FHCSpace Space = Room(Name, First->Min.X, First->Min.Y, First->Max.X, First->Max.Y, Sector, Ceiling);
		for (const FIntRect* Part = First + 1; Part != Parts.end(); ++Part)
		{
			Space.ExtraRects.Add(*Part);
		}
		Space.Holes.Append(Holes.begin(), static_cast<int32>(Holes.size()));
		Space.LightColor = Light;
		Space.LightScale = LightScale;
		L.Spaces.Add(Space);
	};
	auto R = [](int32 X0, int32 Y0, int32 X1, int32 Y1) { return FIntRect(X0, Y0, X1, Y1); };

	// --- Rooms. The bow (bridge) points west, the stern (reactor) east; the plan reads north-up like Kepler-7.
	//     Bridge: stepped like the nose of the ship. Its main part carries the seat circle and the button.
	AddRoom(TEXT("Bruecke"), S::Command, 450.f, { R(8, 20, 17, 40), R(5, 22, 8, 38), R(2, 25, 5, 35) }, FLinearColor(0.72f, 0.84f, 1.f));
	//     North row: quarters (L), admin (stretched hexagon), comms (L)
	AddRoom(TEXT("Quartiere"), S::North, 350.f, { R(14, 3, 26, 13), R(26, 3, 37, 8) }, FLinearColor(1.f, 0.78f, 0.55f), 0.85f);
	AddRoom(TEXT("Leitstand"), S::North, 450.f, { R(39, 1, 60, 10), R(37, 3, 39, 9), R(60, 3, 62, 9) }, FLinearColor(0.68f, 0.92f, 1.f));
	AddRoom(TEXT("Kommunikation"), S::North, 400.f, { R(66, 2, 80, 13), R(62, 2, 66, 8) }, FLinearColor(0.6f, 0.74f, 1.f), 0.9f);
	//     Middle: security (L, dim: the monitors are its light), medbay (octagon with a bay), the cross-shaped atrium,
	//     bio lab (L), cargo hold with two container stacks, and the reactor around its core
	AddRoom(TEXT("Sicherheit"), S::SouthWest, 350.f, { R(19, 16, 33, 25), R(27, 25, 36, 30) }, FLinearColor(1.f, 0.72f, 0.66f), 0.6f);
	AddRoom(TEXT("Medbay"), S::SouthWest, 400.f, { R(20, 34, 33, 44), R(22, 32, 31, 34), R(33, 34, 36, 40) }, FLinearColor(0.82f, 1.f, 0.92f), 1.1f);
	AddRoom(TEXT("Atrium"), S::East, 650.f, { R(46, 13, 54, 47), R(36, 26, 46, 38), R(54, 26, 64, 38) }, FLinearColor(0.88f, 0.92f, 1.f), 0.8f);
	AddRoom(TEXT("BioLabor"), S::East, 400.f, { R(64, 16, 80, 26), R(64, 26, 70, 31) }, FLinearColor(0.68f, 1.f, 0.72f), 0.9f);
	AddRoom(TEXT("Lager"), S::East, 500.f, { R(66, 33, 80, 44), R(64, 34, 66, 38) }, FLinearColor(1.f, 0.72f, 0.42f), 0.75f,
		{ R(69, 36, 71, 41), R(74, 36, 76, 41) });
	AddRoom(TEXT("Hangar"), S::SouthWest, 750.f, { R(14, 47, 37, 59), R(37, 50, 52, 59) }, FLinearColor(0.84f, 0.9f, 1.f), 0.75f);
	// The main part is the walkway north of the core (label and safe point must not land inside the core)
	AddRoom(TEXT("Reaktor"), S::East, 600.f, { R(80, 22, 98, 27), R(80, 27, 98, 38) }, FLinearColor(1.f, 0.5f, 0.3f), 1.f, { R(86, 27, 92, 33) });

	// --- Corridors: two long ones that step up and down around the atrium, short ones to the bridge and the reactor
	L.Spaces.Add(Corridor(TEXT("BugGangNord"), 11, 16, 14, 20, S::Command));
	L.Spaces.Add(Corridor(TEXT("BugGangSued"), 11, 40, 14, 44, S::Command));
	L.Spaces.Add(Corridor(TEXT("GangBackbordVorn"), 11, 13, 40, 16, S::North));
	L.Spaces.Add(Corridor(TEXT("GangBackbordMitte"), 37, 10, 64, 13, S::North));
	L.Spaces.Add(Corridor(TEXT("GangBackbordHeck"), 61, 13, 88, 16, S::North));
	L.Spaces.Add(Corridor(TEXT("GangSteuerbordVorn"), 11, 44, 40, 47, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("GangSteuerbordMitte"), 37, 47, 64, 50, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("GangSteuerbordHeck"), 61, 44, 88, 47, S::SouthWest));
	L.Spaces.Add(Corridor(TEXT("HeckGangNord"), 84, 16, 87, 22, S::East));
	L.Spaces.Add(Corridor(TEXT("HeckGangSued"), 84, 38, 87, 44, S::East));

	// --- Doors: every room has two or more; 6 can be locked, and everything stays reachable with all 6 locked
	L.Doors.Add(DoorH(TEXT("Bruecke_Nord"), 11, 20));
	L.Doors.Add(DoorH(TEXT("Bruecke_Sued"), 11, 40));
	L.Doors.Add(DoorH(TEXT("Quartiere_Sued"), 18, 13, true));
	L.Doors.Add(DoorV(TEXT("Quartiere_Ost"), 37, 4));
	L.Doors.Add(DoorH(TEXT("Leitstand_Sued"), 48, 10));
	L.Doors.Add(DoorV(TEXT("Leitstand_Ost"), 62, 4));
	L.Doors.Add(DoorH(TEXT("Kommunikation_Sued"), 72, 13, true));
	L.Doors.Add(DoorH(TEXT("Sicherheit_Nord"), 24, 16));
	L.Doors.Add(DoorV(TEXT("Sicherheit_Ost"), 36, 26, true));
	L.Doors.Add(DoorH(TEXT("Medbay_Sued"), 23, 44));
	L.Doors.Add(DoorV(TEXT("Medbay_Ost"), 36, 35));
	L.Doors.Add(DoorH(TEXT("Atrium_Nord"), 48, 13));
	L.Doors.Add(DoorH(TEXT("Atrium_Sued"), 48, 47));
	L.Doors.Add(DoorH(TEXT("BioLabor_Nord"), 70, 16, true));
	L.Doors.Add(DoorV(TEXT("BioLabor_West"), 64, 27));
	L.Doors.Add(DoorV(TEXT("BioLabor_Ost"), 80, 22));
	L.Doors.Add(DoorH(TEXT("Lager_Sued"), 71, 44, true));
	L.Doors.Add(DoorV(TEXT("Lager_West"), 64, 35));
	L.Doors.Add(DoorH(TEXT("Hangar_Nord"), 24, 47));
	L.Doors.Add(DoorH(TEXT("Hangar_Ost"), 44, 50));
	L.Doors.Add(DoorH(TEXT("Reaktor_Nord"), 84, 22, true));
	L.Doors.Add(DoorH(TEXT("Reaktor_Sued"), 84, 38));

	L.ChargingStations.Add(Prop(TEXT("L1_Quartiere"), 14.4, 9.0, 0.f));
	L.ChargingStations.Add(Prop(TEXT("L2_Hangar"), 51.6, 55.0, 180.f));
	L.ChargingStations.Add(Prop(TEXT("L3_Reaktor"), 80.4, 30.0, 0.f));
	L.EmergencyButtons.Add(Prop(TEXT("Notfall_Knopf"), 12.5, 30.0, 0.f));
	L.FuseBoxes.Add({ TEXT("SK_Nord"), S::North, FVector2D(42.0, 10.22), 90.f });
	L.FuseBoxes.Add({ TEXT("SK_Ost"), S::East, FVector2D(84.22, 18.5), 0.f });
	L.FuseBoxes.Add({ TEXT("SK_SuedWest"), S::SouthWest, FVector2D(58.0, 49.78), -90.f });

	// --- Security and admin: the camera desk against the west wall of security, the plan table in the middle of the admin room
	L.SecurityConsoles.Add(Prop(TEXT("Sicherheit_Monitore"), 19.6, 21.0, 0.f));
	L.AdminTables.Add(Prop(TEXT("Leitstand_Tisch"), 49.5, 5.0, 90.f));
	auto Camera = [&L](const TCHAR* Id, const FText& Label, double X, double Y, float Height, float Yaw, float Pitch)
	{
		FHCCameraDef C;
		C.Id = Id;
		C.Label = Label;
		C.Position = FVector2D(X, Y);
		C.Height = Height;
		C.Yaw = Yaw;
		C.Pitch = Pitch;
		L.Cameras.Add(C);
	};
	// Only corridors and the open atrium are watched (docs/01 §6): whoever wants to kill unseen does it in a room
	Camera(TEXT("K1"), NSLOCTEXT("HC", "CamPortFore", "Gang Backbord"), 38.8, 14.2, 228.f, 180.f, -14.f);
	Camera(TEXT("K2"), NSLOCTEXT("HC", "CamPortAft", "Gang Backbord Heck"), 62.0, 14.9, 228.f, 0.f, -14.f);
	Camera(TEXT("K3"), NSLOCTEXT("HC", "CamStarboard", "Gang Steuerbord"), 11.9, 45.2, 228.f, 2.f, -14.f);
	Camera(TEXT("K4"), NSLOCTEXT("HC", "CamAtrium", "Atrium"), 53.1, 14.0, 520.f, 104.f, -22.f);

	// --- Task stations (Yaw: 0 = faces east, 90 = south, 180 = west, -90 = north; 0.4 m from the wall line)
	L.TaskStations.Add(Station(TEXT("Security_RegisterBadge"), 0, 22.0, 24.6, -90.f));  // Sicherheit, south wall
	L.TaskStations.Add(Station(TEXT("Power_Reroute"), 1, 31.0, 29.6, -90.f));           // Sicherheit, bay south wall
	L.TaskStations.Add(Station(TEXT("Reactor_Valves"), 0, 97.6, 27.0, 180.f));         // Reaktor, stern wall north
	L.TaskStations.Add(Station(TEXT("Reactor_Calibrate"), 0, 97.6, 33.0, 180.f));      // Reaktor, stern wall south
	L.TaskStations.Add(Station(TEXT("Sample_Analyze"), 0, 68.0, 33.4, 90.f));           // Lager, north wall
	L.TaskStations.Add(Station(TEXT("Storage_ScanCargo"), 0, 79.6, 38.5, 180.f));       // Lager, east wall
	L.TaskStations.Add(Station(TEXT("Sample_Analyze"), 1, 79.6, 18.0, 180.f));          // Bio-Labor, east wall
	L.TaskStations.Add(Station(TEXT("BioLab_Cooling"), 0, 65.0, 16.4, 90.f));           // Bio-Labor, north wall west
	L.TaskStations.Add(Station(TEXT("Sample_Analyze"), 2, 20.4, 39.0, 0.f));            // Medbay, west wall
	L.TaskStations.Add(Station(TEXT("Medbay_MemoryTest"), 0, 30.0, 43.6, -90.f));       // Medbay, south wall
	L.TaskStations.Add(Station(TEXT("Comms_AlignAntenna"), 0, 74.0, 2.4, 90.f));        // Kommunikation, north wall
	L.TaskStations.Add(Station(TEXT("Comms_TransmitData"), 0, 79.6, 8.0, 180.f));       // Kommunikation, east wall
	L.TaskStations.Add(Station(TEXT("Comms_TransmitData"), 1, 14.4, 53.0, 0.f));        // Hangar, west wall
	L.TaskStations.Add(Station(TEXT("Hangar_RepairWiring"), 0, 47.0, 58.6, -90.f));     // Hangar, south wall of the bay
	L.TaskStations.Add(Station(TEXT("Quarters_AirFilter"), 0, 17.0, 3.4, 90.f));        // Quartiere, north wall
	L.TaskStations.Add(Station(TEXT("Power_Reroute"), 0, 32.0, 3.4, 90.f));             // Quartiere, north wall of the wing
	L.TaskStations.Add(Station(TEXT("Atrium_Lights"), 0, 40.0, 26.4, 90.f));            // Atrium, west arm north wall

	// --- Furniture by hand: every room its own things (X, Y in metres, Yaw, height cm, scale, solid, standing lamp)
	auto Put = [&L](const TCHAR* Key, double X, double Y, float Yaw, float Height = 0.f, float Scale = 1.f, bool bSolid = true, bool bLamp = false)
	{
		FHCPlacedProp P;
		P.Key = Key;
		P.Position = FVector2D(X, Y);
		P.YawDegrees = Yaw;
		P.Height = Height;
		P.Scale = Scale;
		P.bSolid = bSolid;
		P.bLamp = bLamp;
		L.PlacedProps.Add(P);
	};
	auto Glow = [&L](double X, double Y, float Height, const FLinearColor& Color, float Candela, float Radius, bool bPulse = false)
	{
		FHCPlacedLight Light;
		Light.Position = FVector2D(X, Y);
		Light.Height = Height;
		Light.Color = Color;
		Light.Candela = Candela;
		Light.Radius = Radius;
		Light.bPulse = bPulse;
		L.PlacedLights.Add(Light);
	};
	// Bridge: a row of flight consoles in the nose, glowing blue, lockers on the flanks; the middle stays free for the seats
	Put(TEXT("Task0"), 2.75, 27.4, 0.f, 0.f, 5.5f);
	Put(TEXT("Task0"), 2.75, 30.0, 0.f, 0.f, 5.5f);
	Put(TEXT("Task0"), 2.75, 32.6, 0.f, 0.f, 5.5f);
	Glow(4.0, 30.0, 140.f, FLinearColor(0.35f, 0.6f, 1.f), 30.f, 650.f);
	Put(TEXT("Case"), 5.75, 23.0, 90.f);
	Put(TEXT("Case"), 5.75, 37.0, 90.f);
	Put(TEXT("Extinguisher"), 16.55, 21.0, 180.f);
	Put(TEXT("Bollard"), 16.4, 38.6, 180.f, 0.f, 2.4f, true, true);
	Put(TEXT("Bollard"), 8.6, 21.4, 0.f, 0.f, 2.4f, true, true);
	// Quarters: lockers and sleeping mats, a work light somebody left on, warm and untidy
	Put(TEXT("Case"), 25.35, 3.75, 0.f);
	Put(TEXT("Case"), 25.35, 5.0, 0.f);
	Put(TEXT("Toolbox"), 25.3, 3.8, 15.f, 50.f);
	Put(TEXT("RolledMat"), 15.0, 11.6, 90.f);
	Put(TEXT("RolledMat"), 15.75, 11.9, 75.f);
	Put(TEXT("OxygenTank"), 14.65, 12.35, 20.f);
	Put(TEXT("Drum"), 27.2, 7.35, 0.f);
	Put(TEXT("WorkLight"), 24.6, 12.1, 210.f);
	Glow(24.0, 11.4, 150.f, FLinearColor(1.f, 0.8f, 0.55f), 40.f, 600.f);
	Put(TEXT("Bollard"), 21.6, 3.6, 90.f, 0.f, 2.4f, true, true);
	// Admin: wall consoles left and right of the plan table, slim lamps in the corners
	Put(TEXT("Task0"), 43.0, 1.55, 90.f, 0.f, 5.5f);
	Put(TEXT("Task0"), 56.0, 1.55, 90.f, 0.f, 5.5f);
	Put(TEXT("Monolith"), 39.7, 1.6, 90.f, 0.f, 3.f, true, true);
	Put(TEXT("Monolith"), 59.3, 1.6, 90.f, 0.f, 3.f, true, true);
	Put(TEXT("Task1"), 39.45, 8.4, 0.f, 0.f, 2.f);
	// Comms: three server racks along the west wall, a toolbox on a case in the wing
	Put(TEXT("Task1"), 66.45, 9.6, 0.f, 0.f, 2.2f);
	Put(TEXT("Task1"), 66.45, 10.6, 0.f, 0.f, 2.2f);
	Put(TEXT("Task1"), 66.45, 11.6, 0.f, 0.f, 2.2f);
	Put(TEXT("Case"), 63.0, 7.3, 0.f);
	Put(TEXT("Toolbox"), 63.0, 7.3, 40.f, 50.f);
	Put(TEXT("WireReel"), 78.9, 12.0, 0.f);
	Put(TEXT("Bollard"), 79.4, 3.0, 180.f, 0.f, 2.4f, true, true);
	// Security: the room is lit by its monitors; a rack in the corner, a slim lamp
	Put(TEXT("Task1"), 32.5, 16.5, 90.f, 0.f, 2.f);
	Put(TEXT("Monolith"), 19.6, 16.6, 0.f, 0.f, 3.f, true, true);
	Put(TEXT("Case"), 35.3, 29.3, 90.f);
	// Medbay: slim gas bottles, a covered tank, a stretcher-like case, cold light over the middle
	Put(TEXT("Nitrogen"), 21.0, 34.9, 0.f);
	Put(TEXT("Nitrogen"), 21.55, 34.75, 40.f);
	Put(TEXT("Nitrogen"), 22.1, 34.9, 80.f);
	Put(TEXT("TankCovered"), 32.2, 41.4, 0.f);
	Put(TEXT("Case"), 26.5, 38.5, 90.f, 0.f, 1.3f);
	Glow(26.5, 38.5, 260.f, FLinearColor(0.75f, 1.f, 0.9f), 30.f, 500.f);
	// Atrium: four standing lamps around the light column in the middle, crates on a pallet in the east arm
	Put(TEXT("Bollard"), 48.2, 28.2, 45.f, 0.f, 1.f, true, true);
	Put(TEXT("Bollard"), 51.8, 28.2, 135.f, 0.f, 1.f, true, true);
	Put(TEXT("Bollard"), 48.2, 31.8, -45.f, 0.f, 1.f, true, true);
	Put(TEXT("Bollard"), 51.8, 31.8, -135.f, 0.f, 1.f, true, true);
	Glow(50.0, 30.0, 420.f, FLinearColor(0.8f, 0.9f, 1.f), 70.f, 1500.f, true);
	Put(TEXT("Pallet"), 62.6, 36.7, 10.f);
	Put(TEXT("Case"), 62.6, 36.7, 10.f, 17.f);
	Put(TEXT("Drum"), 37.0, 37.2, 0.f);
	Put(TEXT("Drum"), 37.6, 37.3, 60.f);
	// Bio lab: a big culture tank glowing green, bottles along the north wall, a case in the bay
	Put(TEXT("TankBig"), 75.5, 23.4, 0.f);
	Glow(75.5, 23.4, 150.f, FLinearColor(0.3f, 1.f, 0.4f), 28.f, 450.f, true);
	Put(TEXT("Nitrogen"), 67.5, 16.6, 0.f);
	Put(TEXT("Nitrogen"), 68.05, 16.6, 30.f);
	Put(TEXT("Case"), 65.1, 30.3, 0.f);
	Put(TEXT("Toolbox"), 65.1, 30.3, -20.f, 50.f);
	// Cargo hold: the container stacks are part of the room; pallets with drums and cases at the ends of the alleys
	Put(TEXT("Pallet"), 78.6, 42.6, 0.f);
	Put(TEXT("Drum"), 78.3, 42.3, 0.f, 17.f);
	Put(TEXT("Drum"), 78.9, 42.3, 40.f, 17.f);
	Put(TEXT("Drum"), 78.6, 42.9, 80.f, 17.f);
	Put(TEXT("Pallet"), 67.4, 42.6, 0.f);
	Put(TEXT("Case"), 67.4, 42.6, 0.f, 17.f);
	Put(TEXT("Drum"), 79.3, 33.7, 0.f);
	Glow(72.5, 38.5, 330.f, FLinearColor(1.f, 0.62f, 0.25f), 35.f, 700.f);
	Glow(67.5, 34.5, 330.f, FLinearColor(1.f, 0.62f, 0.25f), 25.f, 500.f);
	// Hangar: the bay door (gate panels along the south wall) under warning beacons, fuel tanks, crates, a work light
	for (int32 Gate = 0; Gate < 6; ++Gate)
	{
		Put(TEXT("Gate"), 17.0 + Gate * 1.41, 58.44, 0.f, 0.f, 1.f, false);
	}
	Put(TEXT("Beacon"), 19.0, 58.5, 0.f, 420.f, 1.f, false);
	Put(TEXT("Beacon"), 24.0, 58.5, 0.f, 420.f, 1.f, false);
	Glow(21.5, 57.6, 450.f, FLinearColor(1.f, 0.35f, 0.1f), 30.f, 700.f, true);
	Put(TEXT("TankBig"), 35.1, 49.1, 0.f);
	Put(TEXT("TankBig"), 50.1, 51.95, 0.f);
	Put(TEXT("Pallet"), 20.0, 50.0, 20.f);
	Put(TEXT("Case"), 20.0, 50.0, 20.f, 17.f);
	Put(TEXT("Truss"), 30.5, 53.0, 30.f);
	Put(TEXT("Drum"), 32.0, 58.2, 0.f);
	Put(TEXT("Drum"), 32.6, 58.25, 40.f);
	Put(TEXT("TankCovered"), 15.1, 57.9, 0.f);
	Put(TEXT("WorkLight"), 41.0, 57.9, 300.f);
	Glow(41.0, 57.2, 160.f, FLinearColor(1.f, 0.82f, 0.55f), 40.f, 700.f);
	// Reactor: power cells along the core, which glows from all four sides, fuel tanks in the corners at the stern
	Put(TEXT("PowerCells"), 91.9, 25.2, 0.f, 0.f, 0.85f, false);
	Put(TEXT("PowerCells"), 86.15, 34.8, 180.f, 0.f, 0.85f, false);
	Glow(85.4, 30.0, 230.f, FLinearColor(1.f, 0.35f, 0.08f), 80.f, 850.f, true);
	Glow(92.6, 30.0, 230.f, FLinearColor(1.f, 0.35f, 0.08f), 80.f, 850.f, true);
	Glow(89.0, 26.4, 230.f, FLinearColor(1.f, 0.35f, 0.08f), 80.f, 850.f, true);
	Glow(89.0, 33.6, 230.f, FLinearColor(1.f, 0.35f, 0.08f), 80.f, 850.f, true);
	Put(TEXT("TankBig"), 96.15, 23.85, 0.f);
	Put(TEXT("TankBig"), 96.15, 36.15, 0.f);
	Put(TEXT("PipeValve"), 81.6, 37.6, 90.f, 0.f, 1.f);
	Put(TEXT("Extinguisher"), 82.6, 22.45, 90.f);

	return L;
}

FHCStationLayout FHCStationLayout::MakeLobby()
{
	using namespace HCLayout;
	FHCStationLayout L;
	L.MapName = TEXT("Lobby");
	L.bAutoRoomProps = false;
	L.bAutoCorridorProps = false;
	L.SizeX = 16;
	L.SizeY = 12;
	L.SpawnRoom = TEXT("Aufenthaltsraum");
	L.Spaces.Add(Room(TEXT("Aufenthaltsraum"), 0, 0, 16, 12, EHCSector::Command));
	// Loose things put by hand, deliberately uneven: no group repeats and the lamps are not in the corners. All within
	// 1.6 m of the walls, so the spawn circle (3 m around the centre) and the figures of the main menu stay free.
	// Y = 0 is the north wall; the main menu camera stands at the west end and looks east.
	auto Put = [&L](const TCHAR* Key, double X, double Y, float Yaw, bool bLamp = false)
	{
		FHCPlacedProp P;
		P.Key = Key;
		P.Position = FVector2D(X, Y);
		P.YawDegrees = Yaw;
		P.bLamp = bLamp;
		L.PlacedProps.Add(P);
	};
	// North wall: covered tank with a bottle beside it, a lamp a little further east
	Put(TEXT("TankCovered"), 3.3, 1.0, 0.f);
	Put(TEXT("OxygenTank"), 4.55, 0.68, 25.f);
	Put(TEXT("Bollard"), 5.7, 0.62, 0.f, true);
	// North-east: three drums pushed together, the extinguisher further down the east wall
	Put(TEXT("Drum"), 15.05, 0.95, 0.f);
	Put(TEXT("Drum"), 14.47, 0.9, 50.f);
	Put(TEXT("Drum"), 15.1, 1.52, 110.f);
	Put(TEXT("Extinguisher"), 15.4, 2.8, 180.f);
	Put(TEXT("Bollard"), 15.4, 4.3, 90.f, true);
	// East wall, south part: the second tank on its own, turned along the wall
	Put(TEXT("TankCovered"), 15.05, 9.05, 90.f);
	// South wall: two drums near the corner, a lamp, and the second extinguisher far to the west
	Put(TEXT("Drum"), 14.2, 11.15, 20.f);
	Put(TEXT("Drum"), 14.75, 11.2, 75.f);
	Put(TEXT("Bollard"), 12.9, 11.38, 180.f, true);
	Put(TEXT("Extinguisher"), 4.3, 11.4, -90.f);
	// West wall: a single drum near the south corner, a lamp, and a lonely bottle in the north half
	Put(TEXT("Drum"), 0.8, 10.6, 30.f);
	Put(TEXT("Bollard"), 0.62, 8.9, 270.f, true);
	Put(TEXT("OxygenTank"), 0.66, 2.6, 60.f);
	return L;
}

FHCStationLayout FHCStationLayout::MakeByName(FName Name)
{
	if (Name == TEXT("Lobby"))
	{
		return MakeLobby();
	}
	if (Name == TEXT("Ikarus"))
	{
		return MakeIkarus();
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
		for (const FIntRect& Part : Space.GetRects())
		{
			for (int32 Y = Part.Min.Y; Y < Part.Max.Y; ++Y)
			{
				for (int32 X = Part.Min.X; X < Part.Max.X; ++X)
				{
					if (!Space.ContainsCell(X, Y))
					{
						continue; // a hole: solid, walled all round
					}
					if (X < 0 || Y < 0 || X >= Layout.SizeX || Y >= Layout.SizeY)
					{
						BuildProblems.Add(FString::Printf(TEXT("Space %s leaves the map at (%d, %d)"), *Space.Name.ToString(), X, Y));
						continue;
					}
					int32& Cell = Cells[Y * Layout.SizeX + X];
					if (Cell == SpaceIndex)
					{
						continue; // parts of one space may touch or overlap
					}
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
		for (const FIntRect& Part : Space.GetRects())
		{
			const int32 MinSide = FMath::Min(Part.Width(), Part.Height());
			if (Space.bCorridor && MinSide * 100 < 200)
			{
				Problems.Add(FString::Printf(TEXT("Corridor %s is narrower than 200 cm"), *Space.Name.ToString()));
			}
		}
		const FIntPoint Middle((Space.Rect.Min.X + Space.Rect.Max.X) / 2, (Space.Rect.Min.Y + Space.Rect.Max.Y) / 2);
		if (!Space.ContainsCell(Middle.X, Middle.Y))
		{
			Problems.Add(FString::Printf(TEXT("The middle of the main part of %s is not walkable"), *Space.Name.ToString()));
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
		for (const FHCPropDef& Prop : Layout.SecurityConsoles)
		{
			Points.Add({ FString::Printf(TEXT("Camera desk %s"), *Prop.Name.ToString()), Prop.Position, Prop.YawDegrees });
		}
		for (const FHCPropDef& Prop : Layout.AdminTables)
		{
			Points.Add({ FString::Printf(TEXT("Admin table %s"), *Prop.Name.ToString()), Prop.Position, Prop.YawDegrees });
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

	// Cameras hang inside the station, never in a task room (docs/01 §6)
	for (const FHCCameraDef& Camera : Layout.Cameras)
	{
		const int32 Space = GetSpaceAt(FMath::FloorToInt32(Camera.Position.X), FMath::FloorToInt32(Camera.Position.Y));
		if (Space == INDEX_NONE)
		{
			Problems.Add(FString::Printf(TEXT("Camera %s hangs outside the station"), *Camera.Id.ToString()));
		}
		else if (Camera.Height >= Layout.Spaces[Space].CeilingHeight - 20.f)
		{
			Problems.Add(FString::Printf(TEXT("Camera %s hangs in the ceiling"), *Camera.Id.ToString()));
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
