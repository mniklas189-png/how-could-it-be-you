#include "Misc/AutomationTest.h"
#include "Meeting/HCVoteTally.h"
#include "Roles/HCWinConditionEvaluator.h"
#include "World/HCStationLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCMeetingTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	// Players 1..7; A = 1, B = 2
	constexpr int32 A = 1;
	constexpr int32 B = 2;
	constexpr int32 Skip = FHCVoteTally::SkipId;

	static TArray<int32> Players(int32 Count)
	{
		TArray<int32> Result;
		for (int32 i = 1; i <= Count; ++i)
		{
			Result.Add(i);
		}
		return Result;
	}

	/** Voters 1..N vote for the listed targets in order; fewer targets than voters = the rest does not vote. */
	static TMap<int32, int32> Votes(std::initializer_list<int32> Targets)
	{
		TMap<int32, int32> Result;
		int32 Voter = 1;
		for (const int32 Target : Targets)
		{
			Result.Add(Voter++, Target);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoteTallyTest, "HowCouldItBeYou.Meeting.VoteTally", HCMeetingTests::Flags)
bool FHCVoteTallyTest::RunTest(const FString& Parameters)
{
	using namespace HCMeetingTests;

	// The 9 cases of docs/02 §8.6
	FHCVoteOutcome Out = FHCVoteTally::Count(Players(7), Players(7), Votes({ A, A, A, B, B, Skip, Skip }));
	TestEqual(TEXT("1 Clear relative majority: A 3, B 2, skip 2 -> A"), Out.EjectedId, A);
	TestEqual(TEXT("1 A has 3 votes"), Out.Counts[A], 3);
	TestEqual(TEXT("1 Voters of A are known"), Out.VotersByTarget[A].Num(), 3);

	Out = FHCVoteTally::Count(Players(5), Players(5), Votes({ A, A, B, B, Skip }));
	TestEqual(TEXT("2 Tie between two players -> nobody"), Out.EjectedId, static_cast<int32>(INDEX_NONE));

	Out = FHCVoteTally::Count(Players(5), Players(5), Votes({ A, A, Skip, Skip, Skip }));
	TestEqual(TEXT("3 Skip in front: A 2, skip 3 -> nobody"), Out.EjectedId, static_cast<int32>(INDEX_NONE));

	Out = FHCVoteTally::Count(Players(4), Players(4), Votes({ A, A, Skip, Skip }));
	TestEqual(TEXT("4 Tie with skip: A 2, skip 2 -> nobody"), Out.EjectedId, static_cast<int32>(INDEX_NONE));

	Out = FHCVoteTally::Count(Players(5), Players(5), Votes({ A, A }));
	TestEqual(TEXT("5 Non-voters count as skip: A 2, three do not vote -> nobody"), Out.EjectedId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("5 Skip has 3"), Out.Counts[Skip], 3);

	Out = FHCVoteTally::Count(Players(6), Players(6), {});
	TestEqual(TEXT("6 Nobody votes -> nobody"), Out.EjectedId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("6 All 6 are skips"), Out.Counts[Skip], 6);

	// B (2) disconnected: not a candidate and not a voter any more; two votes for B become skips
	TArray<int32> WithoutB = Players(5);
	WithoutB.Remove(B);
	TMap<int32, int32> ForB;
	ForB.Add(1, B);
	ForB.Add(3, B);
	ForB.Add(4, 5);
	Out = FHCVoteTally::Count(WithoutB, WithoutB, ForB);
	TestEqual(TEXT("7 Votes for a disconnected player count as skip"), Out.Counts[Skip], 3);
	TestEqual(TEXT("7 Nobody is ejected"), Out.EjectedId, static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("7 The disconnected player is not in the result"), Out.Counts.Contains(B));

	// Player 7 is dead: its vote for A is ignored, so A 2 vs skip 2 is a tie
	TMap<int32, int32> WithDead = Votes({ A, A, Skip, Skip });
	WithDead.Add(7, A);
	Out = FHCVoteTally::Count(Players(4), Players(4), WithDead);
	TestEqual(TEXT("8 The vote of a dead player is ignored"), Out.Counts[A], 2);
	TestEqual(TEXT("8 So nobody is ejected"), Out.EjectedId, static_cast<int32>(INDEX_NONE));

	Out = FHCVoteTally::Count(Players(3), Players(3), Votes({ B, A, Skip }));
	TestEqual(TEXT("9 Only 3 alive: A 1, B 1, skip 1 -> nobody"), Out.EjectedId, static_cast<int32>(INDEX_NONE));

	// A few more: one vote against zero skips is enough, and voting for yourself is a normal vote
	Out = FHCVoteTally::Count(Players(2), Players(2), Votes({ B, B }));
	TestEqual(TEXT("Unanimous vote ejects"), Out.EjectedId, B);
	Out = FHCVoteTally::Count(Players(4), Players(4), Votes({ B, B, B, Skip }));
	TestEqual(TEXT("3 of 4 against B -> B"), Out.EjectedId, B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCEjectionWinTest, "HowCouldItBeYou.Meeting.EjectionAndWin", HCMeetingTests::Flags)
bool FHCEjectionWinTest::RunTest(const FString& Parameters)
{
	FText Reason;
	// 5 players, the monster is ejected: crew wins at once with the right reason
	FHCWinInput In;
	In.AliveCrew = 4;
	In.AliveMonsters = 0;
	In.bMonsterEjected = true;
	TestEqual(TEXT("Monster ejected -> crew wins"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::Crew);
	TestTrue(TEXT("Reason names the ejection"), Reason.ToString().Contains(TEXT("ausgeworfen")));

	// A crew member is ejected: 4 players -> 2 crew vs 1 monster, the round goes on
	In = FHCWinInput();
	In.AliveCrew = 2;
	In.AliveMonsters = 1;
	TestEqual(TEXT("Crew ejected, 2 vs 1 -> continue"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::None);
	// Ejecting the wrong one at 2 vs 1 hands the monster the win
	In.AliveCrew = 1;
	TestEqual(TEXT("Crew ejected, 1 vs 1 -> monster wins"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::Monster);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCEmergencyButtonLayoutTest, "HowCouldItBeYou.Meeting.ButtonInMeetingRoom", HCMeetingTests::Flags)
bool FHCEmergencyButtonLayoutTest::RunTest(const FString& Parameters)
{
	const FHCStationLayout Layout = FHCStationLayout::MakeKepler7();
	TestEqual(TEXT("Kepler-7 has exactly one emergency button"), Layout.EmergencyButtons.Num(), 1);
	const FHCSpace* Room = Layout.FindSpace(Layout.SpawnRoom);
	if (TestNotNull(TEXT("Meeting room exists"), Room) && Layout.EmergencyButtons.Num() == 1)
	{
		// The seats stand on a 3 m circle around the room centre: the button in the middle is 3 m from every seat
		const FVector2D Center((Room->Rect.Min.X + Room->Rect.Max.X) * 0.5, (Room->Rect.Min.Y + Room->Rect.Max.Y) * 0.5);
		TestTrue(TEXT("Button stands in the middle of the meeting room"), FVector2D::Distance(Center, Layout.EmergencyButtons[0].Position) < 0.5);
		TestTrue(TEXT("Room is wide enough for the seat circle"), Room->Rect.Width() >= 8 && Room->Rect.Height() >= 8);
	}
	// A button outside the meeting room is a layout error
	FHCStationLayout Broken = Layout;
	Broken.EmergencyButtons[0].Position = FVector2D(1.0, 14.0);
	bool bFound = false;
	for (const FString& Problem : FHCStationGrid(Broken).Validate())
	{
		bFound |= Problem.Contains(TEXT("Emergency button"));
	}
	TestTrue(TEXT("Validation reports a button outside the meeting room"), bFound);
	TestEqual(TEXT("Kepler-7 itself is valid"), FHCStationGrid(Layout).Validate().Num(), 0);
	return true;
}

#endif
