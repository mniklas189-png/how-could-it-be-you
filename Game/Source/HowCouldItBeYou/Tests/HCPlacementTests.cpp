#include "Misc/AutomationTest.h"
#include "Audio/HCProceduralAudio.h"
#include "Audio/HCSoundVolumes.h"
#include "Tasks/HCTaskTypes.h"
#include "World/HCMapCatalog.h"
#include "World/HCModelAssets.h"
#include "World/HCPlacement.h"
#include "World/HCStationBuilder.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCPlacementTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	/** Made-up models for the tests that must not depend on the model packs: a 100 x 60 x 80 cm box and a drum. */
	bool TestShapes(FName Key, FHCPropShape& Out)
	{
		if (Key == TEXT("Box"))
		{
			Out.Min = FVector(-50.0, -30.0, 0.0);
			Out.Max = FVector(50.0, 30.0, 80.0);
			return true;
		}
		if (Key == TEXT("Drum"))
		{
			Out.Min = FVector(-25.0, -25.0, 0.0);
			Out.Max = FVector(25.0, 25.0, 90.0);
			return true;
		}
		// Enough of the real keys for a generated corner group (a lamp and a covered tank)
		if (Key == TEXT("Bollard"))
		{
			Out.Min = FVector(-4.0, -4.0, 0.0);
			Out.Max = FVector(4.0, 4.0, 42.0);
			return true;
		}
		if (Key == TEXT("TankCovered"))
		{
			Out.Min = FVector(-50.0, -44.0, 0.0);
			Out.Max = FVector(50.0, 44.0, 95.0);
			return true;
		}
		return false;
	}

	FHCPlacedProp Placed(const TCHAR* Key, double X, double Y, float Yaw = 0.f, float Height = 0.f)
	{
		FHCPlacedProp P;
		P.Key = Key;
		P.Position = FVector2D(X, Y);
		P.YawDegrees = Yaw;
		P.Height = Height;
		return P;
	}

	/** One room of 12 x 10 m with a corridor and a door on its east side. */
	FHCStationLayout TestRoom()
	{
		FHCStationLayout L;
		L.SizeX = 20;
		L.SizeY = 12;
		L.SpawnRoom = TEXT("A");
		L.bAutoRoomProps = false;
		L.bAutoCorridorProps = false;
		FHCSpace A;
		A.Name = TEXT("A");
		A.Rect = FIntRect(0, 0, 12, 10);
		FHCSpace C;
		C.Name = TEXT("GangOst");
		C.Rect = FIntRect(12, 0, 15, 10);
		C.bCorridor = true;
		C.CeilingHeight = 300.f;
		L.Spaces = { A, C };
		FHCDoorDef D;
		D.Name = TEXT("A_Ost");
		D.Start = FIntPoint(12, 4);
		D.bVertical = true;
		D.Width = 3;
		L.Doors = { D };
		return L;
	}

	bool Has(const TArray<FString>& Problems, const TCHAR* Needle)
	{
		return Problems.ContainsByPredicate([Needle](const FString& P) { return P.Contains(Needle); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCPlacementNoOverlapsTest, "HowCouldItBeYou.Placement.NoOverlaps", HCPlacementTests::Flags)
bool FHCPlacementNoOverlapsTest::RunTest(const FString& Parameters)
{
	// Every map as the players get it: with the real models (their real sizes) and the real wall pieces. Nothing may
	// stand inside something else, in a wall, in a doorway, on a seat or where somebody stands to use a device.
	const bool bModels = HCModels::Get() != nullptr;
	if (!bModels)
	{
		AddInfo(TEXT("No model packs on this PC: only devices, vents, doors and seats are checked"));
	}
	TArray<FName> Layouts = { TEXT("Lobby") };
	for (const FHCMapInfo& Map : HCMaps::All())
	{
		Layouts.Add(Map.Id);
	}
	for (const FName& Name : Layouts)
	{
		const FHCStationLayout Layout = FHCStationLayout::MakeByName(Name);
		const FHCPlacement Placed = FHCPlacement::Build(Layout, &AHCStationBuilder::FindPropShape, AHCStationBuilder::GetDressedWallFaceDepth(Layout.WallThickness));
		for (const FString& Problem : Placed.FindProblems(Layout))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Name.ToString(), *Problem));
		}
		AddInfo(FString::Printf(TEXT("%s: %d props, %d generated groups left out because their place was taken"), *Name.ToString(), Placed.Props.Num(), Placed.LeftOut.Num()));
		for (const FString& Note : Placed.LeftOut)
		{
			AddInfo(FString::Printf(TEXT("%s, left out: %s"), *Name.ToString(), *Note));
		}
		if (bModels)
		{
			// A key without a model would silently not be built
			TSet<FName> Checked;
			for (const FHCPlacedProp& Prop : Layout.PlacedProps)
			{
				FHCPropShape Shape;
				if (!Checked.Contains(Prop.Key))
				{
					Checked.Add(Prop.Key);
					TestTrue(FString::Printf(TEXT("%s: there is a model for %s"), *Name.ToString(), *Prop.Key.ToString()), AHCStationBuilder::FindPropShape(Prop.Key, Shape));
				}
			}
		}
		// Every space has a spot a player can be rescued to, and it is really free
		for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
		{
			bool bFound = false;
			const FVector2D Spot = Placed.FindSafeSpot(Layout, SpaceIndex, &bFound);
			TestTrue(FString::Printf(TEXT("%s: %s has a free safe spot"), *Name.ToString(), *Layout.Spaces[SpaceIndex].Name.ToString()), bFound && Placed.IsFreeForPlayer(Spot * 100.0, 45.0));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCPlacementDetectsTest, "HowCouldItBeYou.Placement.DetectsProblems", HCPlacementTests::Flags)
bool FHCPlacementDetectsTest::RunTest(const FString& Parameters)
{
	using namespace HCPlacementTests;
	const double Face = 10.0;
	{
		// Clean: two boxes next to each other, a drum standing on a box
		FHCStationLayout L = TestRoom();
		L.PlacedProps = { Placed(TEXT("Box"), 3.0, 8.0), Placed(TEXT("Box"), 4.2, 8.0), Placed(TEXT("Drum"), 3.0, 8.0, 0.f, 80.f) };
		const FHCPlacement P = FHCPlacement::Build(L, &TestShapes, Face);
		const TArray<FString> Problems = P.FindProblems(L);
		for (const FString& Problem : Problems)
		{
			AddError(TEXT("Clean room: ") + Problem);
		}
		TestEqual(TEXT("Three props built"), P.Props.Num(), 3);
	}
	{
		// Two boxes in each other; a turned box that only overlaps because of its turn is found too
		FHCStationLayout L = TestRoom();
		L.PlacedProps = { Placed(TEXT("Box"), 3.0, 8.0), Placed(TEXT("Box"), 3.6, 8.2), Placed(TEXT("Box"), 8.0, 8.0), Placed(TEXT("Box"), 8.76, 8.0, 90.f) };
		const TArray<FString> Problems = FHCPlacement::Build(L, &TestShapes, Face).FindProblems(L);
		TestTrue(TEXT("Overlapping boxes are found"), Has(Problems, TEXT("Box at (3.0, 8.0) in A stands in Box at (3.6, 8.2)")));
		TestTrue(TEXT("A turned box is measured turned"), Has(Problems, TEXT("Box at (8.0, 8.0) in A stands in Box at (8.8, 8.0)")));
	}
	{
		// In the doorway, on a seat, sunk into the floor, standing in a device and on its standing spot
		FHCStationLayout L = TestRoom();
		L.TaskStations.Add({ TEXT("T"), 0, FVector2D(6.0, 0.4), 90.f });
		L.PlacedProps = { Placed(TEXT("Box"), 11.2, 5.5), Placed(TEXT("Drum"), 9.0, 5.0), Placed(TEXT("Drum"), 2.0, 2.0, 0.f, -30.f), Placed(TEXT("Box"), 6.0, 1.6), Placed(TEXT("Drum"), 6.7, 0.5) };
		const TArray<FString> Problems = FHCPlacement::Build(L, &TestShapes, Face).FindProblems(L);
		TestTrue(TEXT("A box in a doorway is found"), Has(Problems, TEXT("stands in the doorway A_Ost")));
		TestTrue(TEXT("A drum on a seat is found"), Has(Problems, TEXT("stands in seat 1 of the meeting room")));
		TestTrue(TEXT("A drum in the floor is found"), Has(Problems, TEXT("sinks 30 cm into the floor")));
		TestTrue(TEXT("A box where the player stands at a station is found"), Has(Problems, TEXT("stands in the standing spot of task station T#0")));
		TestTrue(TEXT("A drum inside a station is found"), Has(Problems, TEXT("Drum at (6.7, 0.5) in A stands in task station T#0")));
	}
	{
		// A prop put right at a wall is moved out of it, also out of the much deeper real wall pieces
		FHCStationLayout L = TestRoom();
		L.PlacedProps = { Placed(TEXT("Box"), 0.1, 3.0) };
		const FHCPlacement P = FHCPlacement::Build(L, &TestShapes, 38.0);
		TestTrue(TEXT("Moved clear of the wall face"), P.Props.Num() == 1 && P.Props[0].Position.X >= 0.38 + 0.5);
		TestTrue(TEXT("No problem left"), P.FindProblems(L).IsEmpty());
		// ... but a hole in the room (a container stack) is a wall the prop is not moved out of: that is reported
		L.Spaces[0].Holes.Add(FIntRect(5, 4, 7, 6));
		L.PlacedProps = { Placed(TEXT("Box"), 4.8, 5.0) };
		TestTrue(TEXT("A box in the wall of a block inside the room is found"), Has(FHCPlacement::Build(L, &TestShapes, 38.0).FindProblems(L), TEXT("stands in the wall at")));
	}
	{
		// Generated things give way: with a box by hand in a corner the corner group is left out instead of standing in it
		FHCStationLayout L = TestRoom();
		L.bAutoRoomProps = true;
		const int32 Free = FHCPlacement::Build(L, &TestShapes, Face).Props.Num();
		L.PlacedProps = { Placed(TEXT("Box"), 1.0, 1.0), Placed(TEXT("Box"), 2.3, 0.8) };
		const FHCPlacement P = FHCPlacement::Build(L, &TestShapes, Face);
		TestTrue(TEXT("Corner groups are generated"), Free > 0);
		TestTrue(TEXT("A group was left out"), P.LeftOut.Num() > 0);
		for (const FString& Problem : P.FindProblems(L))
		{
			AddError(TEXT("Generated props must never overlap: ") + Problem);
		}
	}
	{
		// The middle of the room is taken (the emergency button): the safe spot moves to the nearest free place
		FHCStationLayout L = TestRoom();
		L.EmergencyButtons.Add({ TEXT("Knopf"), FVector2D(6.0, 5.0), 0.f });
		const FHCPlacement P = FHCPlacement::Build(L, &TestShapes, Face);
		const FVector2D Spot = P.FindSafeSpot(L, 0);
		TestFalse(TEXT("The middle itself is not free"), P.IsFreeForPlayer(FVector2D(600.0, 500.0), 45.0));
		TestTrue(TEXT("The safe spot is free and close"), P.IsFreeForPlayer(Spot * 100.0, 45.0) && FVector2D::Distance(Spot, FVector2D(6.0, 5.0)) < 2.0);
	}
	// The measuring itself
	FHCSolid A, B;
	A.Half = FVector2D(50.0, 50.0);
	A.ZMax = 100.f;
	B = A;
	B.Center = FVector2D(90.0, 0.0);
	TestTrue(TEXT("Boxes 10 cm into each other"), FMath::IsNearlyEqual(FHCPlacement::Penetration(A, B), 10.0, 0.01));
	B.ZMin = 100.f;
	B.ZMax = 180.f;
	TestTrue(TEXT("Standing on top is not inside"), FHCPlacement::Penetration(A, B) <= 0.0);
	B.ZMin = 0.f;
	B.bRound = true;
	B.Center = FVector2D(95.0, 95.0);
	TestTrue(TEXT("A cylinder at the corner of a box is apart where its square bounds would touch"), FHCPlacement::Penetration(A, B) < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutEuropaValidTest, "HowCouldItBeYou.Layout.Europa.Valid", HCPlacementTests::Flags)
bool FHCLayoutEuropaValidTest::RunTest(const FString& Parameters)
{
	const FHCStationLayout Layout = FHCStationLayout::MakeEuropa();
	const FHCStationGrid Grid(Layout);
	for (const FString& Problem : Grid.Validate())
	{
		AddError(Problem);
	}
	TestEqual(TEXT("Ten rooms like the other maps"), Layout.Spaces.FilterByPredicate([](const FHCSpace& S) { return !S.bCorridor; }).Num(), 10);
	TestEqual(TEXT("Six doors can be locked"), Layout.Doors.FilterByPredicate([](const FHCDoorDef& D) { return D.bSabotageable; }).Num(), 6);
	TestTrue(TEXT("At least 3 charging stations"), Layout.ChargingStations.Num() >= 3);
	const int32 Shaped = Layout.Spaces.FilterByPredicate([](const FHCSpace& S) { return !S.bCorridor && (S.ExtraRects.Num() > 0 || S.Holes.Num() > 0); }).Num();
	TestTrue(TEXT("The rooms are not plain boxes"), Shaped >= 8);
	TestTrue(TEXT("Cameras to watch"), Layout.Cameras.Num() >= 4);
	for (const FHCCameraDef& Camera : Layout.Cameras)
	{
		const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Camera.Position.X), FMath::FloorToInt32(Camera.Position.Y));
		const bool bWatchable = Space != INDEX_NONE && (Layout.Spaces[Space].bCorridor || Layout.Spaces[Space].Name == TEXT("Atrium"));
		TestTrue(FString::Printf(TEXT("Camera %s hangs in a corridor or the drill hall"), *Camera.Id.ToString()), bWatchable);
	}
	auto RoomOf = [&](const FVector2D& P) { const int32 S = Grid.GetSpaceAt(FMath::FloorToInt32(P.X), FMath::FloorToInt32(P.Y)); return S != INDEX_NONE ? Layout.Spaces[S].Name : NAME_None; };
	TestEqual(TEXT("One camera desk"), Layout.SecurityConsoles.Num(), 1);
	TestEqual(TEXT("One plan table"), Layout.AdminTables.Num(), 1);
	TestEqual(TEXT("Camera desk in security"), RoomOf(Layout.SecurityConsoles[0].Position), FName(TEXT("Sicherheit")));
	TestEqual(TEXT("Plan table in the radio room"), RoomOf(Layout.AdminTables[0].Position), FName(TEXT("Kommunikation")));
	TestTrue(TEXT("Foggier than the other maps"), Layout.FogDensity > FHCStationLayout::MakeKepler7().FogDensity);

	// Walking times (4-neighbour paths, 3 m/s): the map is as large as the others
	const FHCSpace* Hangar = Layout.FindSpace(TEXT("Hangar"));
	const FHCSpace* Store = Layout.FindSpace(TEXT("Lager"));
	const double EndToEnd = Grid.ShortestPath(FIntPoint(Hangar->Rect.Min.X, Hangar->Rect.Max.Y - 1), FIntPoint(Store->Rect.Max.X - 1, Store->Rect.Min.Y)) / 3.0;
	AddInfo(FString::Printf(TEXT("Vehicle hall -> sample store: %.1f s"), EndToEnd));
	TestTrue(TEXT("End to end reachable and <= 60 s"), EndToEnd > 20.0 && EndToEnd <= 60.0);
	const double MessToShaft = Grid.ShortestPath(Grid.GetSpaceCenter(TEXT("Messe")), Grid.GetSpaceCenter(TEXT("Atrium"))) / 3.0;
	TestTrue(TEXT("Mess -> drill hall is a short way"), MessToShaft > 3.0 && MessToShaft <= 15.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTaskStationsEverywhereTest, "HowCouldItBeYou.Tasks.StationsOnEveryMap", HCPlacementTests::Flags)
bool FHCTaskStationsEverywhereTest::RunTest(const FString& Parameters)
{
	// The task catalog names rooms by id, and every map has a room of every id: every step needs its station on
	// every map the host can choose, in the right room
	const TArray<FHCTaskDefinition> Defs = FHCTaskCatalog::MakeDefaults();
	TestTrue(TEXT("Three maps to choose from"), HCMaps::All().Num() >= 3);
	TestTrue(TEXT("Europa is listed"), HCMaps::Find(TEXT("Europa")) > 0 && HCMaps::Find(TEXT("Map_Europa")) == HCMaps::Find(TEXT("Europa")));
	for (const FHCMapInfo& Map : HCMaps::All())
	{
		const FHCStationLayout Layout = FHCStationLayout::MakeByName(Map.Id);
		const FHCStationGrid Grid(Layout);
		TestEqual(FString::Printf(TEXT("Layout of %s exists"), *Map.Id.ToString()), Layout.MapName, Map.Id);
		int32 Expected = 0;
		for (const FHCTaskDefinition& Def : Defs)
		{
			for (int32 StepIndex = 0; StepIndex < Def.Steps.Num(); ++StepIndex)
			{
				++Expected;
				int32 Found = 0;
				for (const FHCTaskStationDef& Station : Layout.TaskStations)
				{
					if (Station.TaskId == Def.TaskId && Station.StepIndex == StepIndex)
					{
						++Found;
						const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Station.Position.X), FMath::FloorToInt32(Station.Position.Y));
						TestEqual(FString::Printf(TEXT("%s: %s step %d stands in its room"), *Map.Id.ToString(), *Def.TaskId.ToString(), StepIndex),
							Space != INDEX_NONE ? Layout.Spaces[Space].Name : NAME_None, Def.Steps[StepIndex].Room);
					}
				}
				TestEqual(FString::Printf(TEXT("%s: %s step %d has one station"), *Map.Id.ToString(), *Def.TaskId.ToString(), StepIndex), Found, 1);
			}
		}
		TestEqual(FString::Printf(TEXT("%s: no stations without a task step"), *Map.Id.ToString()), Layout.TaskStations.Num(), Expected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutVentsTest, "HowCouldItBeYou.Layout.Vents", HCPlacementTests::Flags)
bool FHCLayoutVentsTest::RunTest(const FString& Parameters)
{
	using namespace HCPlacementTests;
	// Every map of a round has shafts: three of them, each with two to four hatches in different rooms
	for (const FHCMapInfo& Map : HCMaps::All())
	{
		const FHCStationLayout Layout = FHCStationLayout::MakeByName(Map.Id);
		const FHCStationGrid Grid(Layout);
		TMap<int32, TArray<FName>> Shafts;
		for (const FHCVentDef& Vent : Layout.Vents)
		{
			const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Vent.Position.X), FMath::FloorToInt32(Vent.Position.Y));
			const FName Room = Space != INDEX_NONE ? Layout.Spaces[Space].Name : NAME_None;
			TestFalse(FString::Printf(TEXT("%s: %s is not in the meeting room"), *Map.Id.ToString(), *Vent.Id.ToString()), Room == Layout.SpawnRoom);
			TestFalse(FString::Printf(TEXT("%s: only one hatch of a shaft per room (%s)"), *Map.Id.ToString(), *Vent.Id.ToString()), Shafts.FindOrAdd(Vent.Group).Contains(Room));
			Shafts.FindOrAdd(Vent.Group).Add(Room);
		}
		TestEqual(FString::Printf(TEXT("%s has three shafts"), *Map.Id.ToString()), Shafts.Num(), 3);
		TestTrue(FString::Printf(TEXT("%s has at least 8 hatches"), *Map.Id.ToString()), Layout.Vents.Num() >= 8);
	}
	TestEqual(TEXT("No shafts in the lobby"), FHCStationLayout::MakeLobby().Vents.Num(), 0);

	// The rules are checked: a hatch alone in its shaft, one in the meeting room, one at a wall, one in a doorway
	FHCStationLayout L = TestRoom();
	FHCSpace B;
	B.Name = TEXT("B");
	B.Rect = FIntRect(15, 0, 20, 10);
	L.Spaces.Add(B);
	auto Vent = [](const TCHAR* Id, int32 Group, double X, double Y) { FHCVentDef V; V.Id = Id; V.Group = Group; V.Position = FVector2D(X, Y); return V; };
	L.Vents = { Vent(TEXT("Alone"), 1, 17.5, 5.0), Vent(TEXT("Meeting"), 2, 6.0, 5.0), Vent(TEXT("Wall"), 2, 19.5, 8.0), Vent(TEXT("Door"), 2, 13.5, 5.5) };
	const TArray<FString> Problems = FHCStationGrid(L).Validate();
	TestTrue(TEXT("A lonely hatch is found"), Has(Problems, TEXT("Vent shaft 1 has 1 hatches")));
	TestTrue(TEXT("A hatch in the meeting room is found"), Has(Problems, TEXT("Vent Meeting lies in the meeting room")));
	TestTrue(TEXT("A hatch at a wall is found"), Has(Problems, TEXT("Vent Wall is closer than 90 cm to a wall")));
	TestTrue(TEXT("A hatch in a doorway is found"), Has(Problems, TEXT("Vent Door is closer than 200 cm to door A_Ost")));

	// The sounds of the shafts exist and belong to the effects (settings, page "Ton")
	for (const HCProceduralAudio::ESound Sound : { HCProceduralAudio::ESound::VentClank, HCProceduralAudio::ESound::VentCrawl })
	{
		float Seconds = 0.f;
		const TArray<int16> Samples = HCProceduralAudio::Generate(Sound, Seconds);
		int32 Loudest = 0;
		for (const int16 Sample : Samples)
		{
			Loudest = FMath::Max(Loudest, FMath::Abs(static_cast<int32>(Sample)));
		}
		TestTrue(FString::Printf(TEXT("%s can be heard and does not clip"), HCProceduralAudio::Name(Sound)), Seconds > 0.5f && Loudest > 6000 && Loudest < 32000);
		TestEqual(FString::Printf(TEXT("%s is an effect"), HCProceduralAudio::Name(Sound)), HCSoundVolumes::GroupOf(Sound), HCSoundVolumes::EGroup::Effects);
	}
	return true;
}

#endif
