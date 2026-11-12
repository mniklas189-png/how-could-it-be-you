#include "Misc/AutomationTest.h"
#include "Voice/HCVoiceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCVoiceRuleTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
	constexpr EHCVoiceChatRule AllRules[] = { EHCVoiceChatRule::MeetingsOnly, EHCVoiceChatRule::Proximity, EHCVoiceChatRule::Always };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceRuleOutsideRoundTest, "HowCouldItBeYou.Voice.Rules.OutsideRound", HCVoiceRuleTests::Flags)
bool FHCVoiceRuleOutsideRoundTest::RunTest(const FString& Parameters)
{
	// Lobby, loading and result screen: everybody talks to everybody, whatever the rule (also a ghost left from the round)
	for (const EHCVoiceChatRule Rule : HCVoiceRuleTests::AllRules)
	{
		for (const EHCMatchPhase Phase : { EHCMatchPhase::Lobby, EHCMatchPhase::Loading, EHCMatchPhase::GameOver })
		{
			const FString What = FString::Printf(TEXT("rule %d phase %s"), static_cast<int32>(Rule), HCIdentity::PhaseName(Phase));
			TestTrue(What + TEXT(": living -> living"), HCVoiceRules::CanHear(Rule, Phase, true, true));
			TestTrue(What + TEXT(": dead -> living"), HCVoiceRules::CanHear(Rule, Phase, false, true));
			TestTrue(What + TEXT(": may send"), HCVoiceRules::CanSend(Rule, Phase, true));
		}
	}
	TestTrue(TEXT("lobby with proximity: by distance"), HCVoiceRules::UsesDistance(EHCVoiceChatRule::Proximity, EHCMatchPhase::Lobby));
	TestFalse(TEXT("lobby with always: equally loud"), HCVoiceRules::UsesDistance(EHCVoiceChatRule::Always, EHCMatchPhase::Lobby));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceRuleDeadTest, "HowCouldItBeYou.Voice.Rules.Dead", HCVoiceRuleTests::Flags)
bool FHCVoiceRuleDeadTest::RunTest(const FString& Parameters)
{
	// In a round the dead are never heard by the living, in no rule and no phase; they always hear each other
	for (const EHCVoiceChatRule Rule : HCVoiceRuleTests::AllRules)
	{
		for (const EHCMatchPhase Phase : { EHCMatchPhase::Intro, EHCMatchPhase::RoleReveal, EHCMatchPhase::Playing, EHCMatchPhase::Meeting, EHCMatchPhase::Ejection })
		{
			const FString What = FString::Printf(TEXT("rule %d phase %s"), static_cast<int32>(Rule), HCIdentity::PhaseName(Phase));
			TestFalse(What + TEXT(": dead -> living"), HCVoiceRules::CanHear(Rule, Phase, false, true));
			TestTrue(What + TEXT(": dead -> dead"), HCVoiceRules::CanHear(Rule, Phase, false, false));
			TestTrue(What + TEXT(": the dead may send"), HCVoiceRules::CanSend(Rule, Phase, false));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceRuleMeetingsOnlyTest, "HowCouldItBeYou.Voice.Rules.MeetingsOnly", HCVoiceRuleTests::Flags)
bool FHCVoiceRuleMeetingsOnlyTest::RunTest(const FString& Parameters)
{
	constexpr EHCVoiceChatRule Rule = EHCVoiceChatRule::MeetingsOnly;
	for (const EHCMatchPhase Phase : { EHCMatchPhase::Intro, EHCMatchPhase::RoleReveal, EHCMatchPhase::Playing })
	{
		const FString What = HCIdentity::PhaseName(Phase);
		TestFalse(What + TEXT(": living -> living cut"), HCVoiceRules::CanHear(Rule, Phase, true, true));
		TestFalse(What + TEXT(": living -> dead cut"), HCVoiceRules::CanHear(Rule, Phase, true, false));
		TestFalse(What + TEXT(": the living do not send"), HCVoiceRules::CanSend(Rule, Phase, true));
	}
	for (const EHCMatchPhase Phase : { EHCMatchPhase::Meeting, EHCMatchPhase::Ejection })
	{
		const FString What = HCIdentity::PhaseName(Phase);
		TestTrue(What + TEXT(": living -> living"), HCVoiceRules::CanHear(Rule, Phase, true, true));
		TestTrue(What + TEXT(": living -> dead"), HCVoiceRules::CanHear(Rule, Phase, true, false));
		TestTrue(What + TEXT(": the living send"), HCVoiceRules::CanSend(Rule, Phase, true));
		TestFalse(What + TEXT(": equally loud"), HCVoiceRules::UsesDistance(Rule, Phase));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCVoiceRuleProximityAlwaysTest, "HowCouldItBeYou.Voice.Rules.ProximityAndAlways", HCVoiceRuleTests::Flags)
bool FHCVoiceRuleProximityAlwaysTest::RunTest(const FString& Parameters)
{
	for (const EHCVoiceChatRule Rule : { EHCVoiceChatRule::Proximity, EHCVoiceChatRule::Always })
	{
		for (const EHCMatchPhase Phase : { EHCMatchPhase::Playing, EHCMatchPhase::Meeting })
		{
			const FString What = FString::Printf(TEXT("rule %d phase %s"), static_cast<int32>(Rule), HCIdentity::PhaseName(Phase));
			TestTrue(What + TEXT(": living -> living"), HCVoiceRules::CanHear(Rule, Phase, true, true));
			TestTrue(What + TEXT(": living -> dead"), HCVoiceRules::CanHear(Rule, Phase, true, false));
			TestTrue(What + TEXT(": the living send"), HCVoiceRules::CanSend(Rule, Phase, true));
		}
	}
	TestTrue(TEXT("proximity while playing: by distance"), HCVoiceRules::UsesDistance(EHCVoiceChatRule::Proximity, EHCMatchPhase::Playing));
	TestFalse(TEXT("proximity in a meeting: equally loud"), HCVoiceRules::UsesDistance(EHCVoiceChatRule::Proximity, EHCMatchPhase::Meeting));
	TestFalse(TEXT("always while playing: equally loud"), HCVoiceRules::UsesDistance(EHCVoiceChatRule::Always, EHCMatchPhase::Playing));
	return true;
}

#endif
