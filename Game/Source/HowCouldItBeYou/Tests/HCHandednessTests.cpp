#include "Misc/AutomationTest.h"
#include "Characters/HCHandedness.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCHandednessNameTest, "HowCouldItBeYou.Flashlight.LeftHandNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter)
bool FHCHandednessNameTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("flo_ms"), TEXT("Flo_MS"), TEXT("finn"), TEXT("Finn"), TEXT("flo"), TEXT("floms"), TEXT("FloMS"),
		TEXT("Flo MS"), TEXT("flo.ms"), TEXT("Flo-ms"), TEXT("Flo_MS07"), TEXT("Flooo"), TEXT("Fin"), TEXT("Finn_2"), TEXT("_Flo_"), TEXT("Florian") })
	{
		TestTrue(FString::Printf(TEXT("%s holds the flashlight left"), Name), HCHandedness::IsLeftHandedName(Name));
	}
	for (const TCHAR* Name : { TEXT(""), TEXT("Niklas"), TEXT("Spieler"), TEXT("Wolf"), TEXT("Overflow"), TEXT("Delfin"), TEXT("Fl0"), TEXT("Fi"), TEXT("Bot 2") })
	{
		TestFalse(FString::Printf(TEXT("%s holds the flashlight right"), Name), HCHandedness::IsLeftHandedName(Name));
	}
	TestEqual(TEXT("Name key: letters only, lower case, doubled letters merged"), HCHandedness::NameKey(TEXT("Flo_MS 07")), FString(TEXT("floms")));
	return true;
}

#endif
