#include "Misc/AutomationTest.h"
#include "Meeting/HCChat.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCChatTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCChatSanitizeTest, "HowCouldItBeYou.Meeting.Chat.Sanitize", HCChatTests::Flags)
bool FHCChatSanitizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("trimmed"), HCChat::Sanitize(TEXT("   Ich war im Labor   ")), FString(TEXT("Ich war im Labor")));
	TestEqual(TEXT("line breaks and tabs become one space"), HCChat::Sanitize(TEXT("Tom\r\n\tlügt")), FString(TEXT("Tom lügt")));
	TestEqual(TEXT("runs of spaces merge"), HCChat::Sanitize(TEXT("a     b")), FString(TEXT("a b")));
	TestTrue(TEXT("only spaces = nothing to send"), HCChat::Sanitize(TEXT(" \n\t ")).IsEmpty());
	TestTrue(TEXT("empty stays empty"), HCChat::Sanitize(FString()).IsEmpty());
	// Direction overrides could turn a name around, zero-width marks could hide words
	// (built from code points: the compiler warns about a direction character inside a literal)
	FString Override(TEXT("Lena"));
	Override.AppendChar(static_cast<TCHAR>(0x202E));
	Override += TEXT("ist es");
	TestEqual(TEXT("direction override dropped"), HCChat::Sanitize(Override), FString(TEXT("Lenaist es")));
	FString ZeroWidth(TEXT("Mi"));
	ZeroWidth.AppendChar(static_cast<TCHAR>(0x200B));
	ZeroWidth += TEXT("a");
	TestEqual(TEXT("zero-width space dropped"), HCChat::Sanitize(ZeroWidth), FString(TEXT("Mia")));

	const FString Long = HCChat::Sanitize(FString::ChrN(300, TEXT('x')));
	TestEqual(TEXT("cut to the maximum length"), Long.Len(), HCChat::MaxLength);
	// A space right at the cut is not kept at the end
	const FString AtCut = HCChat::Sanitize(FString::ChrN(HCChat::MaxLength - 1, TEXT('x')) + TEXT(" yz"));
	TestEqual(TEXT("no space at the end after a cut"), AtCut, FString::ChrN(HCChat::MaxLength - 1, TEXT('x')));
	// An emoji takes two UTF-16 units; half of one must not stay at the end
	const FString Emoji = HCChat::Sanitize(FString::ChrN(HCChat::MaxLength - 1, TEXT('x')) + TEXT("\U0001F600"));
	TestEqual(TEXT("no half emoji after a cut"), Emoji, FString::ChrN(HCChat::MaxLength - 1, TEXT('x')));
	TestEqual(TEXT("whole emoji kept"), HCChat::Sanitize(TEXT("ok \U0001F600")), FString(TEXT("ok \U0001F600")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCChatReadersTest, "HowCouldItBeYou.Meeting.Chat.Readers", HCChatTests::Flags)
bool FHCChatReadersTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("living -> living"), HCChat::CanRead(true, true));
	TestTrue(TEXT("living -> ghost"), HCChat::CanRead(true, false));
	TestFalse(TEXT("ghost -> living: never"), HCChat::CanRead(false, true));
	TestTrue(TEXT("ghost -> ghost"), HCChat::CanRead(false, false));

	TestTrue(TEXT("open in the gathering"), HCChat::IsOpen(EHCMatchPhase::Meeting, EHCMeetingStage::Gathering));
	TestTrue(TEXT("open in the discussion"), HCChat::IsOpen(EHCMatchPhase::Meeting, EHCMeetingStage::Discussion));
	TestTrue(TEXT("open while voting"), HCChat::IsOpen(EHCMatchPhase::Meeting, EHCMeetingStage::Voting));
	TestTrue(TEXT("open on the result"), HCChat::IsOpen(EHCMatchPhase::Meeting, EHCMeetingStage::Results));
	TestFalse(TEXT("closed during play"), HCChat::IsOpen(EHCMatchPhase::Playing, EHCMeetingStage::None));
	TestFalse(TEXT("closed during the ejection"), HCChat::IsOpen(EHCMatchPhase::Ejection, EHCMeetingStage::None));
	TestFalse(TEXT("closed in the lobby"), HCChat::IsOpen(EHCMatchPhase::Lobby, EHCMeetingStage::None));
	return true;
}

#endif
