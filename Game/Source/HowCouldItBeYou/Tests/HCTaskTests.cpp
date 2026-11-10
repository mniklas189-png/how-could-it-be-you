#include "Misc/AutomationTest.h"
#include "Core/HCGameSettings.h"
#include "Tasks/HCTaskAssigner.h"
#include "Tasks/HCTaskTypes.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCTaskTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	static TArray<int32> Ids(int32 Count)
	{
		TArray<int32> Result;
		for (int32 i = 0; i < Count; ++i)
		{
			Result.Add(200 + i);
		}
		return Result;
	}

	static const FHCTaskDefinition* Find(const TArray<FHCTaskDefinition>& Defs, FName Id)
	{
		return Defs.FindByPredicate([Id](const FHCTaskDefinition& D) { return D.TaskId == Id; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTaskCatalogTest, "HowCouldItBeYou.Tasks.Catalog", HCTaskTests::Flags)
bool FHCTaskCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FHCTaskDefinition> Defs = FHCTaskCatalog::MakeDefaults();
	TestEqual(TEXT("Prototype set has 9 tasks (docs/06 M3)"), Defs.Num(), 9);
	TestEqual(TEXT("Project settings carry the same set"), UHCGameSettings::Get()->TaskDefinitions.Num(), Defs.Num());

	TSet<FName> Ids;
	bool bHold = false, bSequence = false, bMinigame = false, bMultiStep = false;
	int32 Common = 0, Long = 0, Short = 0;
	for (const FHCTaskDefinition& Def : Defs)
	{
		TestFalse(FString::Printf(TEXT("%s: id is unique"), *Def.TaskId.ToString()), Ids.Contains(Def.TaskId));
		Ids.Add(Def.TaskId);
		TestTrue(FString::Printf(TEXT("%s: has steps and a name"), *Def.TaskId.ToString()), Def.Steps.Num() > 0 && !Def.DisplayName.IsEmpty());
		bMultiStep |= Def.Steps.Num() > 1;
		Common += Def.Category == EHCTaskCategory::Common ? 1 : 0;
		Long += Def.Category == EHCTaskCategory::Long ? 1 : 0;
		Short += Def.Category == EHCTaskCategory::Short ? 1 : 0;
		for (const FHCTaskStepDef& Step : Def.Steps)
		{
			bHold |= Step.Type == EHCTaskStepType::Hold;
			bSequence |= Step.Type == EHCTaskStepType::Sequence;
			bMinigame |= Step.Type == EHCTaskStepType::Minigame;
			TestTrue(FString::Printf(TEXT("%s: step has room, label and duration"), *Def.TaskId.ToString()), !Step.Room.IsNone() && !Step.Label.IsEmpty() && Step.Duration >= 1.f);
			TestTrue(FString::Printf(TEXT("%s: overlay steps name their overlay"), *Def.TaskId.ToString()),
				(Step.Type == EHCTaskStepType::Hold) == (Step.Overlay == EHCTaskOverlayKind::None));
		}
	}
	TestTrue(TEXT("All four task types exist: hold, sequence, minigame, multi-step"), bHold && bSequence && bMinigame && bMultiStep);
	TestTrue(TEXT("Enough tasks for 1 common + 2 long + 3 short"), Common >= 1 && Long >= 2 && Short >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTaskStationsOnKepler7Test, "HowCouldItBeYou.Tasks.StationsOnKepler7", HCTaskTests::Flags)
bool FHCTaskStationsOnKepler7Test::RunTest(const FString& Parameters)
{
	const TArray<FHCTaskDefinition> Defs = FHCTaskCatalog::MakeDefaults();
	const FHCStationLayout Layout = FHCStationLayout::MakeKepler7();
	const FHCStationGrid Grid(Layout);

	// Every step of every task has exactly one station, and it stands in the room the definition names
	int32 ExpectedStations = 0;
	for (const FHCTaskDefinition& Def : Defs)
	{
		for (int32 StepIndex = 0; StepIndex < Def.Steps.Num(); ++StepIndex)
		{
			++ExpectedStations;
			int32 Found = 0;
			for (const FHCTaskStationDef& Station : Layout.TaskStations)
			{
				if (Station.TaskId != Def.TaskId || Station.StepIndex != StepIndex)
				{
					continue;
				}
				++Found;
				const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Station.Position.X), FMath::FloorToInt32(Station.Position.Y));
				const FName Room = Space != INDEX_NONE ? Layout.Spaces[Space].Name : NAME_None;
				TestEqual(FString::Printf(TEXT("%s step %d stands in its room"), *Def.TaskId.ToString(), StepIndex), Room, Def.Steps[StepIndex].Room);
			}
			TestEqual(FString::Printf(TEXT("%s step %d has one station"), *Def.TaskId.ToString(), StepIndex), Found, 1);
		}
	}
	TestEqual(TEXT("No stations without a task step"), Layout.TaskStations.Num(), ExpectedStations);

	// No task in the meeting room (anti-camping, docs/01 §11.1)
	for (const FHCTaskDefinition& Def : Defs)
	{
		TestFalse(FString::Printf(TEXT("%s is not in the command center"), *Def.TaskId.ToString()), Def.GetRooms().Contains(Layout.SpawnRoom));
	}

	// Free space, distances to doors and to each other are part of the layout validation
	for (const FString& Problem : Grid.Validate())
	{
		AddError(Problem);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTaskAssignerRulesTest, "HowCouldItBeYou.Tasks.AssignerRules", HCTaskTests::Flags)
bool FHCTaskAssignerRulesTest::RunTest(const FString& Parameters)
{
	using namespace HCTaskTests;
	FHCTaskAssignInput Input;
	Input.Available = FHCTaskCatalog::MakeDefaults();

	for (int32 Players = 2; Players <= 8; ++Players)
	{
		for (int32 Seed = 0; Seed < 40; ++Seed)
		{
			TArray<FString> Warnings;
			const TMap<int32, TArray<FName>> Lists = FHCTaskAssigner::Assign(Ids(Players), Input, Seed, &Warnings);
			TestEqual(TEXT("No warnings with the full catalog"), Warnings.Num(), 0);
			TestEqual(TEXT("Everyone has a list"), Lists.Num(), Players);
			FName CommonTask;
			for (const TPair<int32, TArray<FName>>& Pair : Lists)
			{
				const TArray<FName>& List = Pair.Value;
				TestEqual(TEXT("6 tasks: 1 common + 2 long + 3 short"), List.Num(), 6);
				int32 Common = 0, Long = 0, Short = 0, Visible = 0;
				TSet<FName> Unique;
				for (const FName Id : List)
				{
					Unique.Add(Id);
					const FHCTaskDefinition* Def = Find(Input.Available, Id);
					if (!TestNotNull(TEXT("Assigned task exists"), Def))
					{
						return false;
					}
					Common += Def->Category == EHCTaskCategory::Common ? 1 : 0;
					Long += Def->Category == EHCTaskCategory::Long ? 1 : 0;
					Short += Def->Category == EHCTaskCategory::Short ? 1 : 0;
					Visible += Def->bVisible ? 1 : 0;
					if (Def->Category == EHCTaskCategory::Common)
					{
						TestTrue(TEXT("Everyone has the same common task"), CommonTask.IsNone() || CommonTask == Id);
						CommonTask = Id;
					}
				}
				TestEqual(TEXT("No task twice"), Unique.Num(), List.Num());
				TestTrue(TEXT("1 common, 2 long, 3 short"), Common == 1 && Long == 2 && Short == 3);
				TestTrue(TEXT("At most one visible task"), Visible <= 1);
				TestTrue(TEXT("Spread over at least 3 rooms"), FHCTaskAssigner::CountAreas(List, Input.Available) >= 3);
			}
		}
	}

	// Same seed, same result; another seed should give someone another list sooner or later
	const TMap<int32, TArray<FName>> A = FHCTaskAssigner::Assign(Ids(6), Input, 77);
	const TMap<int32, TArray<FName>> B = FHCTaskAssigner::Assign(Ids(6), Input, 77);
	bool bSame = true;
	for (const TPair<int32, TArray<FName>>& Pair : A)
	{
		bSame &= B.FindRef(Pair.Key) == Pair.Value;
	}
	TestTrue(TEXT("Deterministic for a seed"), bSame);
	bool bDiffers = false;
	for (int32 Seed = 78; Seed < 90 && !bDiffers; ++Seed)
	{
		const TMap<int32, TArray<FName>> C = FHCTaskAssigner::Assign(Ids(6), Input, Seed);
		for (const TPair<int32, TArray<FName>>& Pair : A)
		{
			bDiffers |= C.FindRef(Pair.Key) != Pair.Value;
		}
	}
	TestTrue(TEXT("Other seeds give other lists"), bDiffers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTaskAssignerShortageTest, "HowCouldItBeYou.Tasks.AssignerShortage", HCTaskTests::Flags)
bool FHCTaskAssignerShortageTest::RunTest(const FString& Parameters)
{
	using namespace HCTaskTests;
	// A map with only 2 short tasks and nothing else: hand out what exists and warn
	FHCTaskAssignInput Input;
	for (const FHCTaskDefinition& Def : FHCTaskCatalog::MakeDefaults())
	{
		if (Def.Category == EHCTaskCategory::Short && Input.Available.Num() < 2)
		{
			Input.Available.Add(Def);
		}
	}
	TArray<FString> Warnings;
	const TMap<int32, TArray<FName>> Lists = FHCTaskAssigner::Assign(Ids(4), Input, 5, &Warnings);
	TestTrue(TEXT("Shortage is reported"), Warnings.Num() >= 3);
	for (const TPair<int32, TArray<FName>>& Pair : Lists)
	{
		TestEqual(TEXT("Everyone gets the 2 tasks that exist"), Pair.Value.Num(), 2);
	}

	// Visible tasks: never more than one per player, even if the pool is full of them
	FHCTaskAssignInput VisibleInput;
	VisibleInput.Available = FHCTaskCatalog::MakeDefaults();
	for (FHCTaskDefinition& Def : VisibleInput.Available)
	{
		Def.bVisible = Def.Category == EHCTaskCategory::Short;
	}
	const TMap<int32, TArray<FName>> VisibleLists = FHCTaskAssigner::Assign(Ids(8), VisibleInput, 9);
	for (const TPair<int32, TArray<FName>>& Pair : VisibleLists)
	{
		int32 Visible = 0;
		for (const FName Id : Pair.Value)
		{
			Visible += Find(VisibleInput.Available, Id)->bVisible ? 1 : 0;
		}
		TestTrue(TEXT("At most one visible task per player"), Visible <= 1);
	}

	// Nobody to assign to
	TestEqual(TEXT("No players, no lists"), FHCTaskAssigner::Assign({}, Input, 1).Num(), 0);
	return true;
}

#endif
