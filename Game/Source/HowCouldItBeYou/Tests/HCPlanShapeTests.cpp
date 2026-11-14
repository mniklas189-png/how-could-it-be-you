#include "Misc/AutomationTest.h"
#include "UI/HCPlanShape.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCPlanShapeTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
}

/** The drawn plan (station map, minimap) shows exactly the floor of the map: every cell once, no hole, nothing outside. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCPlanShapeFloorsTest, "HowCouldItBeYou.Plan.FloorsMatchTheLayout", HCPlanShapeTests::Flags)
bool FHCPlanShapeFloorsTest::RunTest(const FString& Parameters)
{
	for (const FName MapName : { FName(TEXT("Kepler7")), FName(TEXT("Ikarus")) })
	{
		const FHCStationLayout Layout = FHCStationLayout::MakeByName(MapName);
		const FHCStationGrid Grid(Layout);
		FHCPlanShape Shape;
		TestFalse(TEXT("Not built yet"), Shape.IsFor(Layout));
		Shape.Build(Layout);
		TestTrue(TEXT("Built for this layout"), Shape.IsFor(Layout));
		TestEqual(TEXT("One floor per space"), Shape.Floors.Num(), Layout.Spaces.Num());

		// How often each cell is drawn, and for which space
		TArray<int32> Drawn;
		Drawn.Init(INDEX_NONE, Layout.SizeX * Layout.SizeY);
		int32 Twice = 0, Outside = 0, RectCount = 0;
		for (int32 SpaceIndex = 0; SpaceIndex < Shape.Floors.Num(); ++SpaceIndex)
		{
			RectCount += Shape.Floors[SpaceIndex].Num();
			for (const FIntRect& Rect : Shape.Floors[SpaceIndex])
			{
				for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
				{
					for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
					{
						if (X < 0 || Y < 0 || X >= Layout.SizeX || Y >= Layout.SizeY)
						{
							++Outside;
							continue;
						}
						int32& Cell = Drawn[Y * Layout.SizeX + X];
						Twice += Cell != INDEX_NONE ? 1 : 0;
						Cell = SpaceIndex;
					}
				}
			}
		}
		int32 Wrong = 0;
		for (int32 Y = 0; Y < Layout.SizeY; ++Y)
		{
			for (int32 X = 0; X < Layout.SizeX; ++X)
			{
				Wrong += Drawn[Y * Layout.SizeX + X] != Grid.GetSpaceAt(X, Y) ? 1 : 0;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: no cell drawn twice (it would show as a darker patch)"), *MapName.ToString()), Twice, 0);
		TestEqual(*FString::Printf(TEXT("%s: nothing drawn outside the map"), *MapName.ToString()), Outside, 0);
		TestEqual(*FString::Printf(TEXT("%s: every cell drawn as the space it belongs to"), *MapName.ToString()), Wrong, 0);
		// Whole rooms, not single cells: the plan is drawn every frame
		TestTrue(*FString::Printf(TEXT("%s: few rectangles (%d)"), *MapName.ToString(), RectCount), RectCount < Layout.Spaces.Num() * 8);

		// Walls: straight strokes inside the map; every door of the layout is on the plan with its width
		TestTrue(TEXT("Walls"), Shape.Walls.Num() > Layout.Spaces.Num());
		for (const TPair<FVector2D, FVector2D>& Wall : Shape.Walls)
		{
			const bool bStraight = Wall.Key.X == Wall.Value.X || Wall.Key.Y == Wall.Value.Y;
			const bool bInside = Wall.Key.X >= 0.0 && Wall.Key.Y >= 0.0 && Wall.Value.X <= Layout.SizeX && Wall.Value.Y <= Layout.SizeY;
			if (!bStraight || !bInside || Wall.Key == Wall.Value)
			{
				AddError(FString::Printf(TEXT("%s: bad wall %s -> %s"), *MapName.ToString(), *Wall.Key.ToString(), *Wall.Value.ToString()));
			}
		}
		TestEqual(TEXT("Doors"), Shape.Doors.Num(), Layout.Doors.Num());
		for (const FHCDoorDef& Door : Layout.Doors)
		{
			const FHCPlanDoor* OnPlan = Shape.FindDoor(Door.Name);
			if (!TestNotNull(*FString::Printf(TEXT("%s: door %s on the plan"), *MapName.ToString(), *Door.Name.ToString()), OnPlan))
			{
				continue;
			}
			TestEqual(TEXT("Door width"), FVector2D::Distance(OnPlan->From, OnPlan->To), static_cast<double>(Door.Width));
			TestEqual(TEXT("Door can be locked"), OnPlan->bSabotageable, Door.bSabotageable);
		}
	}
	return true;
}

#endif
