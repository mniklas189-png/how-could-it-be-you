#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "HAL/PlatformProcess.h"
#include "Online/HCJoinCode.h"
#include "Online/HCLanDiscovery.h"
#include "Roles/HCRoleAssigner.h"
#include "Roles/HCWinConditionEvaluator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCMatchTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	static TArray<int32> Ids(int32 Count)
	{
		TArray<int32> Result;
		for (int32 i = 0; i < Count; ++i)
		{
			Result.Add(100 + i);
		}
		return Result;
	}

	static int32 CountMonsters(const TMap<int32, EHCRole>& Roles)
	{
		int32 N = 0;
		for (const TPair<int32, EHCRole>& Pair : Roles)
		{
			N += Pair.Value == EHCRole::Monster ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCRoleAssignerOneMonsterTest, "HowCouldItBeYou.Roles.ExactlyOneMonster", HCMatchTests::Flags)
bool FHCRoleAssignerOneMonsterTest::RunTest(const FString& Parameters)
{
	using namespace HCMatchTests;
	for (int32 Players = 4; Players <= 8; ++Players)
	{
		for (int32 Seed = 0; Seed < 50; ++Seed)
		{
			const TMap<int32, EHCRole> Roles = FHCRoleAssigner::Assign(Ids(Players), 1, Seed, {});
			TestEqual(FString::Printf(TEXT("%d players seed %d: everyone has a role"), Players, Seed), Roles.Num(), Players);
			TestEqual(FString::Printf(TEXT("%d players seed %d: exactly 1 monster"), Players, Seed), CountMonsters(Roles), 1);
		}
	}
	// Never everyone a monster
	TestEqual(TEXT("2 players, 5 monsters wanted -> 1"), CountMonsters(FHCRoleAssigner::Assign(Ids(2), 5, 7, {})), 1);
	TestEqual(TEXT("1 player -> crew"), CountMonsters(FHCRoleAssigner::Assign(Ids(1), 1, 7, {})), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCRoleAssignerDeterministicTest, "HowCouldItBeYou.Roles.SeedAndRandomness", HCMatchTests::Flags)
bool FHCRoleAssignerDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace HCMatchTests;
	TestTrue(TEXT("Same seed -> same result"), FHCRoleAssigner::Assign(Ids(6), 1, 1234, {}).OrderIndependentCompareEqual(FHCRoleAssigner::Assign(Ids(6), 1, 1234, {})));

	// Over many seeds every player should be the monster sometimes (no fixed seat)
	TMap<int32, int32> TimesMonster;
	for (int32 Seed = 0; Seed < 800; ++Seed)
	{
		for (const TPair<int32, EHCRole>& Pair : FHCRoleAssigner::Assign(Ids(8), 1, Seed, {}))
		{
			if (Pair.Value == EHCRole::Monster)
			{
				TimesMonster.FindOrAdd(Pair.Key)++;
			}
		}
	}
	for (const int32 Id : Ids(8))
	{
		const int32 N = TimesMonster.FindRef(Id);
		TestTrue(FString::Printf(TEXT("Player %d monster %d/800 times (expected ~100)"), Id, N), N > 50 && N < 150);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCRoleAssignerForcedTest, "HowCouldItBeYou.Roles.ForcedRolesWin", HCMatchTests::Flags)
bool FHCRoleAssignerForcedTest::RunTest(const FString& Parameters)
{
	using namespace HCMatchTests;
	for (int32 Seed = 0; Seed < 30; ++Seed)
	{
		TMap<int32, EHCRole> Forced;
		Forced.Add(103, EHCRole::Monster);
		const TMap<int32, EHCRole> Roles = FHCRoleAssigner::Assign(Ids(6), 1, Seed, Forced);
		TestEqual(TEXT("Forced monster is monster"), Roles.FindRef(103), EHCRole::Monster);
		TestEqual(TEXT("Still exactly one monster"), CountMonsters(Roles), 1);

		TMap<int32, EHCRole> ForcedCrew;
		ForcedCrew.Add(100, EHCRole::Crew);
		const TMap<int32, EHCRole> Roles2 = FHCRoleAssigner::Assign(Ids(4), 1, Seed, ForcedCrew);
		TestEqual(TEXT("Forced crew stays crew"), Roles2.FindRef(100), EHCRole::Crew);
		TestEqual(TEXT("One monster among the others"), CountMonsters(Roles2), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCWinConditionTest, "HowCouldItBeYou.Roles.WinConditions", HCMatchTests::Flags)
bool FHCWinConditionTest::RunTest(const FString& Parameters)
{
	FText Reason;
	FHCWinInput In;
	In.AliveCrew = 5;
	In.AliveMonsters = 1;
	In.TotalTasks = 30;
	In.CompletedTasks = 10;
	TestEqual(TEXT("Running match: nobody wins"), FHCWinConditionEvaluator::Evaluate(In, Reason), EHCWinner::None);

	FHCWinInput Ejected = In;
	Ejected.AliveMonsters = 0;
	Ejected.bMonsterEjected = true;
	TestEqual(TEXT("Monster ejected -> crew wins immediately"), FHCWinConditionEvaluator::Evaluate(Ejected, Reason), EHCWinner::Crew);

	FHCWinInput Left = In;
	Left.AliveMonsters = 0;
	Left.bMonsterLeft = true;
	TestEqual(TEXT("Monster left -> crew wins"), FHCWinConditionEvaluator::Evaluate(Left, Reason), EHCWinner::Crew);
	TestTrue(TEXT("Reason mentions leaving"), Reason.ToString().Contains(TEXT("verlassen")));

	FHCWinInput Parity = In;
	Parity.AliveCrew = 1;
	TestEqual(TEXT("1 crew vs 1 monster -> monster wins (parity)"), FHCWinConditionEvaluator::Evaluate(Parity, Reason), EHCWinner::Monster);
	FHCWinInput TestRound = Parity;
	TestRound.bParityEnabled = false;
	TestEqual(TEXT("2-player test round: 1 vs 1 does not end the round"), FHCWinConditionEvaluator::Evaluate(TestRound, Reason), EHCWinner::None);
	TestRound.CompletedTasks = TestRound.TotalTasks;
	TestEqual(TEXT("2-player test round: tasks still win"), FHCWinConditionEvaluator::Evaluate(TestRound, Reason), EHCWinner::Crew);
	Parity.AliveCrew = 2;
	TestEqual(TEXT("2 crew vs 1 monster -> continue"), FHCWinConditionEvaluator::Evaluate(Parity, Reason), EHCWinner::None);

	FHCWinInput Tasks = In;
	Tasks.CompletedTasks = 30;
	TestEqual(TEXT("All tasks -> crew wins"), FHCWinConditionEvaluator::Evaluate(Tasks, Reason), EHCWinner::Crew);
	Tasks.bCrewWinsByTasks = false;
	TestEqual(TEXT("Tasks don't count in Evacuation"), FHCWinConditionEvaluator::Evaluate(Tasks, Reason), EHCWinner::None);

	FHCWinInput NoTasks = In;
	NoTasks.TotalTasks = 0;
	NoTasks.CompletedTasks = 0;
	TestEqual(TEXT("Empty task list never wins the match"), FHCWinConditionEvaluator::Evaluate(NoTasks, Reason), EHCWinner::None);

	FHCWinInput Timer = In;
	Timer.bCrewWinsByTasks = false;
	Timer.bTimerEnabled = true;
	Timer.bTimerExpired = true;
	Timer.AliveCrew = 2;
	TestEqual(TEXT("Timer with 2 crew alive -> crew"), FHCWinConditionEvaluator::Evaluate(Timer, Reason), EHCWinner::Crew);
	Timer.bTimerEnabled = false;
	TestEqual(TEXT("Timer disabled -> continue"), FHCWinConditionEvaluator::Evaluate(Timer, Reason), EHCWinner::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJoinCodeTest, "HowCouldItBeYou.Online.JoinCode", HCMatchTests::Flags)
bool FHCJoinCodeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Alphabet has 31 characters"), FCString::Strlen(FHCJoinCode::Alphabet()), 31);
	for (const TCHAR Banned : FString(TEXT("0O1IL")))
	{
		int32 Index;
		TestFalse(FString::Printf(TEXT("Alphabet has no %c"), Banned), FString(FHCJoinCode::Alphabet()).FindChar(Banned, Index));
	}

	FRandomStream Random(42);
	for (int32 i = 0; i < 200; ++i)
	{
		const FString Code = FHCJoinCode::Make(Random);
		FString Normalized;
		TestTrue(TEXT("Generated code is valid"), FHCJoinCode::Normalize(Code, Normalized) && Normalized == Code);
	}

	FString Out;
	TestTrue(TEXT("Lower case, space and dash are accepted"), FHCJoinCode::Normalize(TEXT("k7m-4qx"), Out) && Out == TEXT("K7M4QX"));
	TestTrue(TEXT("Formatted with a space"), FHCJoinCode::Normalize(TEXT(" K7M 4QX "), Out) && Out == TEXT("K7M4QX"));
	TestFalse(TEXT("Confusable O is rejected"), FHCJoinCode::Normalize(TEXT("K7MOQX"), Out));
	TestFalse(TEXT("Too short is rejected"), FHCJoinCode::Normalize(TEXT("K7M4Q"), Out));
	TestEqual(TEXT("Format"), FHCJoinCode::Format(TEXT("K7M4QX")), FString(TEXT("K7M 4QX")));

	TestTrue(TEXT("IPv4 is an address"), FHCJoinCode::LooksLikeAddress(TEXT("192.168.0.5")));
	TestTrue(TEXT("IPv4 with port is an address"), FHCJoinCode::LooksLikeAddress(TEXT("127.0.0.1:7777")));
	TestTrue(TEXT("localhost is an address"), FHCJoinCode::LooksLikeAddress(TEXT("localhost")));
	TestFalse(TEXT("A join code is not an address"), FHCJoinCode::LooksLikeAddress(TEXT("K7M4QX")));
	TestFalse(TEXT("Octet > 255 is not an address"), FHCJoinCode::LooksLikeAddress(TEXT("192.168.0.300")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLanPacketTest, "HowCouldItBeYou.Online.LanSearchPackets", HCMatchTests::Flags)
bool FHCLanPacketTest::RunTest(const FString& Parameters)
{
	FString Code;
	TestTrue(TEXT("Query round trip"), HCLanDiscovery::ParseQuery(HCLanDiscovery::MakeQuery(TEXT("K7M4QX")), Code) && Code == TEXT("K7M4QX"));
	TestFalse(TEXT("Query with an invalid code"), HCLanDiscovery::ParseQuery(TEXT("HCLAN1|Q|K7MOQX"), Code));
	TestFalse(TEXT("Query of another game"), HCLanDiscovery::ParseQuery(TEXT("OTHER|Q|K7M4QX"), Code));
	TestFalse(TEXT("An answer is no query"), HCLanDiscovery::ParseQuery(TEXT("HCLAN1|A|K7M4QX|0.11.8|LOBBY|7777"), Code));
	TestFalse(TEXT("Empty packet"), HCLanDiscovery::ParseQuery(FString(), Code));

	HCLanDiscovery::FAnswer Answer;
	Answer.Code = TEXT("K7M4QX");
	Answer.BuildVersion = TEXT("0.11.8");
	Answer.bMatchRunning = true;
	Answer.GamePort = 7778;
	HCLanDiscovery::FAnswer Read;
	TestTrue(TEXT("Answer round trip"), HCLanDiscovery::ParseAnswer(HCLanDiscovery::MakeAnswer(Answer), Read));
	TestEqual(TEXT("Answer code"), Read.Code, Answer.Code);
	TestEqual(TEXT("Answer version"), Read.BuildVersion, Answer.BuildVersion);
	TestTrue(TEXT("Answer match running"), Read.bMatchRunning);
	TestEqual(TEXT("Answer port"), Read.GamePort, 7778);

	Answer.BuildVersion = TEXT("1|2");
	TestTrue(TEXT("A | in the version does not break the answer"), HCLanDiscovery::ParseAnswer(HCLanDiscovery::MakeAnswer(Answer), Read) && Read.GamePort == 7778);
	TestFalse(TEXT("Port 0"), HCLanDiscovery::ParseAnswer(TEXT("HCLAN1|A|K7M4QX|0.11.8|LOBBY|0"), Read));
	TestFalse(TEXT("Port too large"), HCLanDiscovery::ParseAnswer(TEXT("HCLAN1|A|K7M4QX|0.11.8|LOBBY|70000"), Read));
	TestFalse(TEXT("Unknown state"), HCLanDiscovery::ParseAnswer(TEXT("HCLAN1|A|K7M4QX|0.11.8|PAUSED|7777"), Read));
	TestFalse(TEXT("Lower-case code"), HCLanDiscovery::ParseAnswer(TEXT("HCLAN1|A|k7m4qx|0.11.8|LOBBY|7777"), Read));
	TestFalse(TEXT("A query is no answer"), HCLanDiscovery::ParseAnswer(HCLanDiscovery::MakeQuery(TEXT("K7M4QX")), Read));

	TestTrue(TEXT("Home network adapter"), HCLanDiscovery::IsSearchAdapterAddress(TEXT("192.168.178.40")));
	TestTrue(TEXT("Hyper-V adapter is searched too"), HCLanDiscovery::IsSearchAdapterAddress(TEXT("172.22.208.1")));
	TestFalse(TEXT("Loopback"), HCLanDiscovery::IsSearchAdapterAddress(TEXT("127.0.0.1")));
	TestFalse(TEXT("Link-local"), HCLanDiscovery::IsSearchAdapterAddress(TEXT("169.254.83.107")));
	TestFalse(TEXT("IPv6"), HCLanDiscovery::IsSearchAdapterAddress(TEXT("fe80::1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCLanSocketTest, "HowCouldItBeYou.Online.LanSearchSockets", HCMatchTests::Flags)
bool FHCLanSocketTest::RunTest(const FString& Parameters)
{
	// A host and a search in this process, over real UDP sockets
	FHCLanResponder Host;
	if (!TestTrue(TEXT("Host opens its port"), Host.Start()))
	{
		return false;
	}
	HCLanDiscovery::FAnswer Served;
	Served.Code = TEXT("K7M4QX");
	Served.BuildVersion = TEXT("test");
	Served.GamePort = 7781;

	auto RunSearch = [&Host, &Served](const FString& Code, double Seconds, HCLanDiscovery::FAnswer& OutAnswer, FString& OutIp) -> bool
	{
		FHCLanSearch Search;
		if (!Search.Start(Code))
		{
			return false;
		}
		const double End = FPlatformTime::Seconds() + Seconds;
		while (FPlatformTime::Seconds() < End)
		{
			if (Search.Poll(FPlatformTime::Seconds(), OutAnswer, OutIp))
			{
				return true;
			}
			Host.Poll(Served);
			FPlatformProcess::Sleep(0.01f);
		}
		return false;
	};

	HCLanDiscovery::FAnswer Found;
	FString HostIp;
	TestTrue(TEXT("The search finds the host"), RunSearch(TEXT("K7M4QX"), 3.0, Found, HostIp));
	TestEqual(TEXT("Answer carries the game port"), Found.GamePort, 7781);
	TestEqual(TEXT("Answer carries the version"), Found.BuildVersion, FString(TEXT("test")));
	TestFalse(TEXT("Host address is known"), HostIp.IsEmpty());
	AddInfo(FString::Printf(TEXT("Host answered from %s"), *HostIp));

	TestFalse(TEXT("Another code gets no answer"), RunSearch(TEXT("ABCDEF"), 1.2, Found, HostIp));
	return true;
}

#endif
