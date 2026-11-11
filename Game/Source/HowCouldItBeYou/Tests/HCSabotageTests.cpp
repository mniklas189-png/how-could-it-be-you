#include "Misc/AutomationTest.h"
#include "Characters/HCUneaseComponent.h"
#include "Sabotage/HCSabotageManager.h"
#include "Sabotage/HCSabotageRules.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCSabotageTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCSabotageRulesTest, "HowCouldItBeYou.Sabotage.Rules", HCSabotageTests::Flags)
bool FHCSabotageRulesTest::RunTest(const FString& Parameters)
{
	using R = EHCSabotageReject;
	auto Result = [](TFunctionRef<void(FHCSabotageCheck&)> Change)
	{
		FHCSabotageCheck Check;
		Check.Now = 100.0;
		Check.EarliestTime = 30.0;
		Check.ReadyTime = 90.0;
		Change(Check);
		return FHCSabotageRules::Validate(Check);
	};
	// The checks of docs/02 §11.1 and the fairness rules of docs/01 §7
	TestTrue(TEXT("Everything fine -> allowed"), Result([](FHCSabotageCheck&) {}) == R::None);
	TestTrue(TEXT("Not during intro, meeting, ejection"), Result([](FHCSabotageCheck& C) { C.bPhasePlaying = false; }) == R::NotPlaying);
	TestTrue(TEXT("Crew can never sabotage"), Result([](FHCSabotageCheck& C) { C.bIsMonster = false; }) == R::NotMonster);
	TestTrue(TEXT("Dead monster"), Result([](FHCSabotageCheck& C) { C.bAlive = false; }) == R::Dead);
	TestTrue(TEXT("30 s after round start or meeting end"), Result([](FHCSabotageCheck& C) { C.EarliestTime = 101.0; }) == R::TooEarly);
	TestTrue(TEXT("Cooldown still running"), Result([](FHCSabotageCheck& C) { C.ReadyTime = 100.5; }) == R::OnCooldown);
	TestTrue(TEXT("Cooldown just over"), Result([](FHCSabotageCheck& C) { C.ReadyTime = 100.0; }) == R::None);
	TestTrue(TEXT("Only one sabotage per type"), Result([](FHCSabotageCheck& C) { C.bTypeActive = true; }) == R::AlreadyActive);
	TestTrue(TEXT("Unknown or unmarked target"), Result([](FHCSabotageCheck& C) { C.bTargetValid = false; }) == R::TargetInvalid);
	TestTrue(TEXT("Never the command center"), Result([](FHCSabotageCheck& C) { C.bCommandCenter = true; }) == R::TargetInvalid);
	TestTrue(TEXT("Never the last way"), Result([](FHCSabotageCheck& C) { C.bPathStaysOpen = false; }) == R::WouldCutOff);

	uint8 Sector = 0;
	TestTrue(TEXT("Sector names: Nord"), AHCSabotageManager::ParseSector(TEXT("Nord"), Sector) && Sector == static_cast<uint8>(EHCSector::North));
	TestTrue(TEXT("Sector names: SW"), AHCSabotageManager::ParseSector(TEXT("sw"), Sector) && Sector == static_cast<uint8>(EHCSector::SouthWest));
	TestTrue(TEXT("Sector ids round-trip"), AHCSabotageManager::ParseSector(AHCSabotageManager::SectorTargetId(static_cast<uint8>(EHCSector::East)).ToString(), Sector)
		&& Sector == static_cast<uint8>(EHCSector::East));
	TestFalse(TEXT("The command center is no sabotage target"), AHCSabotageManager::ParseSector(TEXT("Command"), Sector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCSabotageLayoutTest, "HowCouldItBeYou.Sabotage.Kepler7Layout", HCSabotageTests::Flags)
bool FHCSabotageLayoutTest::RunTest(const FString& Parameters)
{
	const FHCStationLayout Layout = FHCStationLayout::MakeKepler7();
	const FHCStationGrid Grid(Layout);
	TestEqual(TEXT("Kepler-7 is valid (incl. all sabotageable doors locked)"), Grid.Validate().Num(), 0);
	TestEqual(TEXT("One fuse box per power sector"), Layout.FuseBoxes.Num(), 3);
	TSet<EHCSector> Seen;
	for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
	{
		const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Box.Position.X), FMath::FloorToInt32(Box.Position.Y));
		const bool bOk = Space != INDEX_NONE && Layout.Spaces[Space].bCorridor && Layout.Spaces[Space].Sector == Box.Sector;
		TestTrue(FString::Printf(TEXT("Fuse box %s hangs in a corridor of its own sector"), *Box.Name.ToString()), bOk);
		Seen.Add(Box.Sector);
	}
	TestFalse(TEXT("No fuse box for the command center"), Seen.Contains(EHCSector::Command));
	TestEqual(TEXT("Three different sectors"), Seen.Num(), 3);

	int32 Sabotageable = 0;
	for (const FHCDoorDef& Door : Layout.Doors)
	{
		Sabotageable += Door.bSabotageable ? 1 : 0;
	}
	TestEqual(TEXT("Six doors can be locked (docs/01 §11.1)"), Sabotageable, 6);

	// A fuse box in a room or in the wrong sector is a layout error
	FHCStationLayout Broken = Layout;
	Broken.FuseBoxes[0].Position = FVector2D(8.0, 20.0); // inside the room "Quartiere"
	bool bFound = false;
	for (const FString& Problem : FHCStationGrid(Broken).Validate())
	{
		bFound |= Problem.Contains(TEXT("Fuse box"));
	}
	TestTrue(TEXT("Validation reports a misplaced fuse box"), bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCUneaseTest, "HowCouldItBeYou.Atmosphere.UneaseHasNoLeak", HCSabotageTests::Flags)
bool FHCUneaseTest::RunTest(const FString& Parameters)
{
	// Tripwire: the input holds exactly two flags and one count. Whoever adds a field has to come past this test
	// and docs/02 §15.5 - a role or a distance to the Wandler must never get in here.
	static_assert(sizeof(FHCUneaseInput) == 8, "FHCUneaseInput changed: check docs/02 §15.5 before adding inputs");

	FHCUneaseInput In;
	In.VisiblePlayers = 2;
	const float Calm = FHCUnease::Compute(In);
	In.VisiblePlayers = 0;
	const float Alone = FHCUnease::Compute(In);
	In.bSectorPowered = false;
	const float AloneDark = FHCUnease::Compute(In);
	In.bFlashlightOn = true;
	const float AloneDarkLight = FHCUnease::Compute(In);
	In.VisiblePlayers = 1;
	const float DarkWithSomeone = FHCUnease::Compute(In);

	TestTrue(TEXT("Lit and in company is calm"), Calm < 0.2f);
	TestTrue(TEXT("Being alone is worse than company"), Alone > Calm);
	TestTrue(TEXT("Alone in the dark is the worst"), AloneDark > Alone && AloneDark >= 0.85f);
	TestTrue(TEXT("The flashlight helps a little"), AloneDarkLight < AloneDark && AloneDarkLight > Alone);
	TestTrue(TEXT("Company helps in the dark"), DarkWithSomeone < AloneDarkLight);
	TestTrue(TEXT("Always within 0..1"), Calm >= 0.f && AloneDark <= 1.f);
	return true;
}

#endif
