#include "Misc/AutomationTest.h"
#include "Tasks/HCTaskTypes.h"
#include "World/HCMapCatalog.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCIkarusTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutIkarusValidTest, "HowCouldItBeYou.Layout.Ikarus.Valid", HCIkarusTests::Flags)
bool FHCLayoutIkarusValidTest::RunTest(const FString& Parameters)
{
	const FHCStationLayout Layout = FHCStationLayout::MakeIkarus();
	const FHCStationGrid Grid(Layout);
	for (const FString& Problem : Grid.Validate())
	{
		AddError(Problem);
	}
	// What the map is for: rooms that are not plain boxes, a camera room and an admin table
	const int32 Shaped = Layout.Spaces.FilterByPredicate([](const FHCSpace& S) { return !S.bCorridor && (S.ExtraRects.Num() > 0 || S.Holes.Num() > 0); }).Num();
	TestTrue(TEXT("Most rooms are not plain boxes"), Shaped >= 8);
	TestTrue(TEXT("Cameras to watch"), Layout.Cameras.Num() >= 4);
	TestEqual(TEXT("One camera desk"), Layout.SecurityConsoles.Num(), 1);
	TestEqual(TEXT("One admin table"), Layout.AdminTables.Num(), 1);
	for (const FHCCameraDef& Camera : Layout.Cameras)
	{
		const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Camera.Position.X), FMath::FloorToInt32(Camera.Position.Y));
		const bool bWatchable = Space != INDEX_NONE && (Layout.Spaces[Space].bCorridor || Layout.Spaces[Space].Name == TEXT("Atrium"));
		TestTrue(FString::Printf(TEXT("Camera %s hangs in a corridor or the open atrium"), *Camera.Id.ToString()), bWatchable);
	}
	auto RoomOf = [&](const FVector2D& P) { const int32 S = Grid.GetSpaceAt(FMath::FloorToInt32(P.X), FMath::FloorToInt32(P.Y)); return S != INDEX_NONE ? Layout.Spaces[S].Name : NAME_None; };
	TestEqual(TEXT("Camera desk in security"), RoomOf(Layout.SecurityConsoles[0].Position), FName(TEXT("Sicherheit")));
	TestEqual(TEXT("Admin table in the admin room"), RoomOf(Layout.AdminTables[0].Position), FName(TEXT("Leitstand")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTaskStationsOnIkarusTest, "HowCouldItBeYou.Tasks.StationsOnIkarus", HCIkarusTests::Flags)
bool FHCTaskStationsOnIkarusTest::RunTest(const FString& Parameters)
{
	// The task catalog names rooms by id; Ikarus has a room of every id, so every step needs its station there
	const TArray<FHCTaskDefinition> Defs = FHCTaskCatalog::MakeDefaults();
	const FHCStationLayout Layout = FHCStationLayout::MakeIkarus();
	const FHCStationGrid Grid(Layout);
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
					TestEqual(FString::Printf(TEXT("%s step %d stands in its room"), *Def.TaskId.ToString(), StepIndex),
						Space != INDEX_NONE ? Layout.Spaces[Space].Name : NAME_None, Def.Steps[StepIndex].Room);
				}
			}
			TestEqual(FString::Printf(TEXT("%s step %d has one station"), *Def.TaskId.ToString(), StepIndex), Found, 1);
		}
	}
	TestEqual(TEXT("No stations without a task step"), Layout.TaskStations.Num(), Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutShapedRoomsTest, "HowCouldItBeYou.Layout.ShapedRooms", HCIkarusTests::Flags)
bool FHCLayoutShapedRoomsTest::RunTest(const FString& Parameters)
{
	// An L-shaped room with a pillar, and a corridor: walls follow the shape, the pillar is walled all round
	FHCStationLayout L;
	L.SizeX = 12;
	L.SizeY = 12;
	L.SpawnRoom = TEXT("A");
	FHCSpace A;
	A.Name = TEXT("A");
	A.Rect = FIntRect(0, 0, 8, 4);
	A.ExtraRects.Add(FIntRect(0, 4, 4, 10));
	A.Holes.Add(FIntRect(1, 6, 3, 8));
	FHCSpace C;
	C.Name = TEXT("C");
	C.Rect = FIntRect(8, 0, 11, 6);
	C.bCorridor = true;
	C.CeilingHeight = 300.f;
	L.Spaces = { A, C };
	const FHCStationGrid Grid(L);
	TestEqual(TEXT("Cell of the second part belongs to A"), Grid.GetSpaceAt(1, 9), 0);
	TestEqual(TEXT("Hole is solid"), Grid.GetSpaceAt(2, 7), INDEX_NONE);
	TestEqual(TEXT("Corner outside the L is empty"), Grid.GetSpaceAt(6, 8), INDEX_NONE);
	TestTrue(TEXT("Walk inside the L, around the pillar"), Grid.ShortestPath(FIntPoint(7, 0), FIntPoint(2, 9)) > 0);
	TestFalse(TEXT("No step into the pillar"), Grid.CanStep(FIntPoint(0, 7), FIntPoint(1, 7)));
	TArray<FHCWallEdge> Edges;
	Grid.CollectEdges(Edges);
	const bool bWallBetweenParts = Edges.ContainsByPredicate([](const FHCWallEdge& E) { return !E.bVertical && E.Cell == FIntPoint(1, 4); });
	TestFalse(TEXT("No wall between two parts of one room"), bWallBetweenParts);
	const bool bPillarWall = Edges.ContainsByPredicate([](const FHCWallEdge& E) { return E.bVertical && E.Cell == FIntPoint(1, 6); });
	TestTrue(TEXT("The pillar has walls"), bPillarWall);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCMapCatalogTest, "HowCouldItBeYou.Layout.MapCatalog", HCIkarusTests::Flags)
bool FHCMapCatalogTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Two maps to choose from"), HCMaps::All().Num() >= 2);
	TestEqual(TEXT("Kepler-7 stays the first (default) map"), HCMaps::Get(0).Id, FName(TEXT("Kepler7")));
	const int32 Ikarus = HCMaps::Find(TEXT("Ikarus"));
	TestTrue(TEXT("Ikarus is listed"), Ikarus > 0);
	TestEqual(TEXT("Found by package name"), HCMaps::Find(TEXT("Map_Ikarus")), Ikarus);
	for (const FHCMapInfo& Map : HCMaps::All())
	{
		TestEqual(FString::Printf(TEXT("Layout of %s exists"), *Map.Id.ToString()), FHCStationLayout::MakeByName(Map.Id).MapName, Map.Id);
	}
	TestEqual(TEXT("Unknown choice falls back to the first map"), HCMaps::Get(-1).Id, FName(TEXT("Kepler7")));
	return true;
}

#endif
