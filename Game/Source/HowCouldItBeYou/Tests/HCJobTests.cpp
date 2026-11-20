#include "Misc/AutomationTest.h"
#include "Characters/HCOutfit.h"
#include "Core/HCRoundOptions.h"
#include "Meeting/HCVoteTally.h"
#include "Roles/HCJobs.h"
#include "World/HCStationLayout.h"
#include "World/HCMapCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCJobTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	TArray<int32> Ids(int32 From, int32 Count)
	{
		TArray<int32> Result;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Result.Add(From + Index);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJobAssignerTest, "HowCouldItBeYou.Jobs.Assigner", HCJobTests::Flags)
bool FHCJobAssignerTest::RunTest(const FString& Parameters)
{
	using namespace HCJobTests;
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		// 7 crew, 1 Wandler, every job on: six crew get six different jobs, one stays without
		const TArray<int32> Crew = Ids(1, 7);
		const TArray<int32> Monsters = { 100 };
		const TMap<int32, EHCJob> Jobs = FHCJobAssigner::Assign(Crew, Monsters, HCJobs::AllMask, Seed);
		TSet<EHCJob> Given;
		int32 Without = 0;
		for (const int32 Id : Crew)
		{
			const EHCJob Job = Jobs.FindRef(Id);
			if (Job == EHCJob::None)
			{
				++Without;
				continue;
			}
			TestFalse(TEXT("No job twice in the crew"), Given.Contains(Job));
			Given.Add(Job);
		}
		TestEqual(TEXT("Six jobs for seven crew: one without"), Without, 1);
		// The cover: none left over (all six are held), so the Wandler has none either
		TestEqual(TEXT("No cover when the crew holds every job"), Jobs.FindRef(100), EHCJob::None);
		TestEqual(TEXT("The same seed gives the same jobs"), FHCJobAssigner::Assign(Crew, Monsters, HCJobs::AllMask, Seed).OrderIndependentCompareEqual(Jobs), true);
	}
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		// 3 crew, 2 Wandler: the covers are jobs nobody in the crew has, never the commander
		const TArray<int32> Crew = Ids(1, 3);
		const TArray<int32> Monsters = { 100, 101 };
		const TMap<int32, EHCJob> Jobs = FHCJobAssigner::Assign(Crew, Monsters, HCJobs::AllMask, Seed);
		TSet<EHCJob> CrewJobs;
		for (const int32 Id : Crew)
		{
			TestNotEqual(TEXT("Everybody of a small crew has a job"), Jobs.FindRef(Id), EHCJob::None);
			CrewJobs.Add(Jobs.FindRef(Id));
		}
		for (const int32 Id : Monsters)
		{
			const EHCJob Cover = Jobs.FindRef(Id);
			TestNotEqual(TEXT("The Wandler gets a cover"), Cover, EHCJob::None);
			TestNotEqual(TEXT("Never the commander as a cover"), Cover, EHCJob::Commander);
			TestFalse(TEXT("The cover is not a job of the crew"), CrewJobs.Contains(Cover));
		}
		TestNotEqual(TEXT("Two Wandler, two different covers"), Jobs.FindRef(100), Jobs.FindRef(101));
	}
	// Only the jobs that are switched on
	const uint8 Mask = HCJobs::Bit(EHCJob::Medic) | HCJobs::Bit(EHCJob::Radio);
	const TMap<int32, EHCJob> Few = FHCJobAssigner::Assign(Ids(1, 5), { 100 }, Mask, 7);
	for (const TPair<int32, EHCJob>& Pair : Few)
	{
		TestTrue(TEXT("Only switched-on jobs"), Pair.Value == EHCJob::None || Pair.Value == EHCJob::Medic || Pair.Value == EHCJob::Radio);
	}
	// Nothing switched on: nobody has a job
	for (const TPair<int32, EHCJob>& Pair : FHCJobAssigner::Assign(Ids(1, 5), { 100 }, 0, 7))
	{
		TestEqual(TEXT("No jobs at all"), Pair.Value, EHCJob::None);
	}
	// Forced (dev flag) wins
	const TMap<int32, EHCJob> Forced = FHCJobAssigner::Assign(Ids(1, 4), { 100 }, HCJobs::AllMask, 3, { { 2, EHCJob::Guard }, { 100, EHCJob::Medic } });
	TestEqual(TEXT("Forced job"), Forced.FindRef(2), EHCJob::Guard);
	TestEqual(TEXT("Forced cover"), Forced.FindRef(100), EHCJob::Medic);
	// Names and ids
	for (int32 Index = 0; Index < HCJobs::Count; ++Index)
	{
		const EHCJob Job = HCJobs::FromIndex(Index);
		TestFalse(TEXT("Every job has a name"), HCJobs::Name(Job).IsEmpty());
		TestEqual(TEXT("Id parses back"), HCJobs::Parse(HCJobs::Id(Job)), Job);
		TestTrue(TEXT("Every job is in the full mask"), (HCJobs::AllMask & HCJobs::Bit(Job)) != 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCCommanderVoteTest, "HowCouldItBeYou.Jobs.CommanderVote", HCJobTests::Flags)
bool FHCCommanderVoteTest::RunTest(const FString& Parameters)
{
	using namespace HCJobTests;
	const TArray<int32> Players = Ids(1, 5);
	const TMap<int32, int32> Weights = { { 1, 2 } };
	// 1 (commander) and 4 vote for 4, 2 and 3 for 5, 5 skips. Counted plainly it is 2 : 2, a tie, nobody goes;
	// with the commander's vote twice it is 3 : 2 and 4 is ejected
	const TMap<int32, int32> Votes = { { 1, 4 }, { 4, 4 }, { 2, 5 }, { 3, 5 } };
	const FHCVoteOutcome Plain = FHCVoteTally::Count(Players, Players, Votes);
	TestEqual(TEXT("Plain: a tie ejects nobody"), Plain.EjectedId, INDEX_NONE);
	const FHCVoteOutcome Weighted = FHCVoteTally::Count(Players, Players, Votes, Weights);
	TestEqual(TEXT("The commander's vote counts twice"), Weighted.Counts.FindRef(4), 3);
	TestEqual(TEXT("The commander tips it"), Weighted.EjectedId, 4);
	TestEqual(TEXT("The commander is one voter, not two"), Weighted.VotersByTarget.FindRef(4).Num(), 2);
	// A commander who skips adds nothing anywhere but to skip
	const FHCVoteOutcome Skipped = FHCVoteTally::Count(Players, Players, { { 2, 5 }, { 3, 5 }, { 4, 5 } }, Weights);
	TestEqual(TEXT("Skip of the commander counts twice too"), Skipped.Counts.FindRef(FHCVoteTally::SkipId), 3);
	TestEqual(TEXT("3 against 3: nobody"), Skipped.EjectedId, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCRoundOptionsTest, "HowCouldItBeYou.Jobs.SecondWandler", HCJobTests::Flags)
bool FHCRoundOptionsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("7 players: one Wandler"), HCRoundOptions::MonstersFor(1, 7, true), 1);
	TestEqual(TEXT("8 players with the option: two"), HCRoundOptions::MonstersFor(1, 8, true), 2);
	TestEqual(TEXT("8 players without the option: one"), HCRoundOptions::MonstersFor(1, 8, false), 1);
	TestEqual(TEXT("Never more than the rules give plus one"), HCRoundOptions::MonstersFor(1, 10, true), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCOutfitTest, "HowCouldItBeYou.Jobs.Outfit", HCJobTests::Flags)
bool FHCOutfitTest::RunTest(const FString& Parameters)
{
	using HCOutfit::EPart;
	uint8 Outfit = 0;
	for (int32 Part = 0; Part < static_cast<int32>(EPart::Count); ++Part)
	{
		const EPart Which = static_cast<EPart>(Part);
		TestEqual(TEXT("Zero = the model's own look"), HCOutfit::Get(0, Which), 0);
		Outfit = HCOutfit::With(Outfit, Which, HCOutfit::Choices(Which) - 1);
	}
	for (int32 Part = 0; Part < static_cast<int32>(EPart::Count); ++Part)
	{
		const EPart Which = static_cast<EPart>(Part);
		TestEqual(TEXT("Every part keeps its own choice"), HCOutfit::Get(Outfit, Which), HCOutfit::Choices(Which) - 1);
		TestFalse(TEXT("Every choice has a name"), HCOutfit::ChoiceName(Which, HCOutfit::Choices(Which) - 1).IsEmpty());
	}
	// Out of range is clamped, never spills into the next part
	const uint8 Hair = HCOutfit::With(0, EPart::Hair, 99);
	TestEqual(TEXT("Clamped to the last hair colour"), HCOutfit::Get(Hair, EPart::Hair), HCOutfit::Choices(EPart::Hair) - 1);
	TestEqual(TEXT("Trousers untouched"), HCOutfit::Get(Hair, EPart::Trousers), 0);
	TestEqual(TEXT("Boots untouched"), HCOutfit::Get(Hair, EPart::Boots), 0);
	// Bits that mean nothing (hair 5..7, boots 3) read as the last real choice
	TestEqual(TEXT("Unknown hair bits"), HCOutfit::Get(0xFF, EPart::Hair), HCOutfit::Choices(EPart::Hair) - 1);
	TestEqual(TEXT("Unknown boot bits"), HCOutfit::Get(0xFF, EPart::Boots), HCOutfit::Choices(EPart::Boots) - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCCrisisLayoutTest, "HowCouldItBeYou.Jobs.MapsHaveEverything", HCJobTests::Flags)
bool FHCCrisisLayoutTest::RunTest(const FString& Parameters)
{
	for (const FHCMapInfo& Map : HCMaps::All())
	{
		const FHCStationLayout Layout = FHCStationLayout::MakeByName(Map.Id);
		int32 Meltdown = 0, Oxygen = 0;
		for (const FHCCrisisPanelDef& Panel : Layout.CrisisPanels)
		{
			Meltdown += Panel.Crisis == static_cast<uint8>(EHCCrisis::Meltdown) ? 1 : 0;
			Oxygen += Panel.Crisis == static_cast<uint8>(EHCCrisis::Oxygen) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("%s: two meltdown panels"), *Map.Id.ToString()), Meltdown, 2);
		TestEqual(FString::Printf(TEXT("%s: two oxygen panels"), *Map.Id.ToString()), Oxygen, 2);
		// The guard needs a picture on every map
		TestTrue(FString::Printf(TEXT("%s: cameras for the guard"), *Map.Id.ToString()), Layout.Cameras.Num() >= 3);
		TestEqual(FString::Printf(TEXT("%s: one camera desk"), *Map.Id.ToString()), Layout.SecurityConsoles.Num(), 1);
		TestEqual(FString::Printf(TEXT("%s: no wardrobe in a round"), *Map.Id.ToString()), Layout.Wardrobes.Num(), 0);
		// The oxygen valves are far apart: one person cannot hold both
		const FHCCrisisPanelDef* A = Layout.CrisisPanels.FindByPredicate([](const FHCCrisisPanelDef& P) { return P.Crisis == 2 && P.Index == 0; });
		const FHCCrisisPanelDef* B = Layout.CrisisPanels.FindByPredicate([](const FHCCrisisPanelDef& P) { return P.Crisis == 2 && P.Index == 1; });
		TestTrue(FString::Printf(TEXT("%s: oxygen valves at least 25 m apart"), *Map.Id.ToString()), A && B && FVector2D::Distance(A->Position, B->Position) >= 25.0);
	}
	const FHCStationLayout Lobby = FHCStationLayout::MakeLobby();
	TestEqual(TEXT("One wardrobe in the lobby"), Lobby.Wardrobes.Num(), 1);
	TestEqual(TEXT("No crisis in the lobby"), Lobby.CrisisPanels.Num(), 0);
	return true;
}

#endif
