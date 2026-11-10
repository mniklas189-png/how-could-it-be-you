#include "Misc/AutomationTest.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutKepler7ValidTest, "HowCouldItBeYou.Layout.Kepler7.Valid", HCTests::Flags)
bool FHCLayoutKepler7ValidTest::RunTest(const FString& Parameters)
{
	const FHCStationLayout Layout = FHCStationLayout::MakeKepler7();
	const FHCStationGrid Grid(Layout);
	const TArray<FString> Problems = Grid.Validate();
	for (const FString& Problem : Problems)
	{
		AddError(Problem);
	}
	TestEqual(TEXT("Room count (10 rooms incl. atrium)"), Layout.Spaces.FilterByPredicate([](const FHCSpace& S) { return !S.bCorridor; }).Num(), 10);
	TestEqual(TEXT("Sabotageable doors (docs/01 §11.1: exactly 6)"), Layout.Doors.FilterByPredicate([](const FHCDoorDef& D) { return D.bSabotageable; }).Num(), 6);
	TestTrue(TEXT("At least 3 charging stations"), Layout.ChargingStations.Num() >= 3);
	return Problems.IsEmpty();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutKepler7WalkTimesTest, "HowCouldItBeYou.Layout.Kepler7.WalkTimes", HCTests::Flags)
bool FHCLayoutKepler7WalkTimesTest::RunTest(const FString& Parameters)
{
	const FHCStationLayout Layout = FHCStationLayout::MakeKepler7();
	const FHCStationGrid Grid(Layout);
	constexpr double WalkMetersPerSecond = 3.0;

	// Grid paths are 4-neighbour (Manhattan), so they are a bit longer than a real walk.
	auto Seconds = [&](FName From, FName To)
	{
		const int32 Meters = Grid.ShortestPath(Grid.GetSpaceCenter(From), Grid.GetSpaceCenter(To));
		return Meters < 0 ? -1.0 : Meters / WalkMetersPerSecond;
	};

	const double CommandToReactor = Seconds(TEXT("Kommandozentrale"), TEXT("Reaktor"));
	AddInfo(FString::Printf(TEXT("Kommandozentrale -> Reaktor: %.1f s (target ~20 s)"), CommandToReactor));
	TestTrue(TEXT("Kommandozentrale -> Reaktor between 15 and 30 s"), CommandToReactor >= 15.0 && CommandToReactor <= 30.0);

	const double HangarToMedbay = Seconds(TEXT("Hangar"), TEXT("Medbay"));
	AddInfo(FString::Printf(TEXT("Hangar -> Medbay: %.1f s (target ~22 s)"), HangarToMedbay));
	TestTrue(TEXT("Hangar -> Medbay between 15 and 32 s"), HangarToMedbay >= 15.0 && HangarToMedbay <= 32.0);

	// End to end: Hangar back wall -> Bio-Labor back wall, shortest way must stay <= 60 s
	const FHCSpace* Hangar = Layout.FindSpace(TEXT("Hangar"));
	const FHCSpace* Bio = Layout.FindSpace(TEXT("BioLabor"));
	const FIntPoint From(Hangar->Rect.Min.X, (Hangar->Rect.Min.Y + Hangar->Rect.Max.Y) / 2);
	const FIntPoint To(Bio->Rect.Max.X - 1, (Bio->Rect.Min.Y + Bio->Rect.Max.Y) / 2);
	const double EndToEnd = Grid.ShortestPath(From, To) / WalkMetersPerSecond;
	AddInfo(FString::Printf(TEXT("End to end (shortest): %.1f s (longest sensible way 45-60 s)"), EndToEnd));
	TestTrue(TEXT("End to end reachable and <= 60 s"), EndToEnd > 0.0 && EndToEnd <= 60.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLayoutDetectsProblemsTest, "HowCouldItBeYou.Layout.DetectsProblems", HCTests::Flags)
bool FHCLayoutDetectsProblemsTest::RunTest(const FString& Parameters)
{
	// A room with one door to a corridor, and an unreachable room: the validator must complain.
	FHCStationLayout L;
	L.SizeX = 20;
	L.SizeY = 20;
	L.SpawnRoom = TEXT("A");
	FHCSpace A; A.Name = TEXT("A"); A.Rect = FIntRect(0, 0, 5, 5);
	FHCSpace C; C.Name = TEXT("C"); C.Rect = FIntRect(5, 1, 10, 4); C.bCorridor = true; C.CeilingHeight = 300.f;
	FHCSpace B; B.Name = TEXT("B"); B.Rect = FIntRect(12, 12, 16, 16);
	L.Spaces = { A, C, B };
	FHCDoorDef D; D.Name = TEXT("A_C"); D.Start = FIntPoint(5, 1); D.bVertical = true; D.Width = 3;
	FHCDoorDef Narrow; Narrow.Name = TEXT("Narrow"); Narrow.Start = FIntPoint(16, 12); Narrow.bVertical = true; Narrow.Width = 1;
	L.Doors = { D, Narrow };

	const TArray<FString> Problems = FHCStationGrid(L).Validate();
	auto Has = [&Problems](const TCHAR* Needle)
	{
		return Problems.ContainsByPredicate([Needle](const FString& P) { return P.Contains(Needle); });
	};
	TestTrue(TEXT("Detects unreachable room"), Has(TEXT("B is not reachable")));
	TestTrue(TEXT("Detects room with < 2 doors"), Has(TEXT("Room A has fewer than 2 doors")));
	TestTrue(TEXT("Detects narrow door"), Has(TEXT("Door Narrow is narrower")));
	TestTrue(TEXT("Detects door not between two spaces"), Has(TEXT("Door Narrow cell 0 does not sit")));
	return true;
}

#endif
