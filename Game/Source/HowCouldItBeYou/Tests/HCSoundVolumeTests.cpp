#include "Misc/AutomationTest.h"
#include "Audio/HCSoundVolumes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCSoundVolumeGroupsTest, "HowCouldItBeYou.Audio.VolumeGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FHCSoundVolumeGroupsTest::RunTest(const FString& Parameters)
{
	using HCProceduralAudio::ESound;
	using HCSoundVolumes::EGroup;
	// The settings page promises these: "Schreck (Jumpscares, Schreie)", "Geräusche (Türen, Schalter)", "Signale (Alarm, Rundenende)"
	TestEqual(TEXT("The kill scream is a scare"), HCSoundVolumes::GroupOf(ESound::Scream), EGroup::Scares);
	TestEqual(TEXT("The jumpscare stinger is a scare"), HCSoundVolumes::GroupOf(ESound::Stinger), EGroup::Scares);
	TestEqual(TEXT("Doors are effects"), HCSoundVolumes::GroupOf(ESound::Door), EGroup::Effects);
	TestEqual(TEXT("The flashlight switch is an effect"), HCSoundVolumes::GroupOf(ESound::Click), EGroup::Effects);
	TestEqual(TEXT("The sabotage alarm is a signal"), HCSoundVolumes::GroupOf(ESound::Alarm), EGroup::Signals);
	TestEqual(TEXT("The end of the round is a signal"), HCSoundVolumes::GroupOf(ESound::Victory), EGroup::Signals);

	// Every group keeps its volume under a name of its own, apart from the background volume
	TSet<FString> Keys;
	for (int32 Group = 0; Group < static_cast<int32>(EGroup::Count); ++Group)
	{
		Keys.Add(HCSoundVolumes::OptionKey(static_cast<EGroup>(Group)));
	}
	Keys.Add(HCSoundVolumes::BackgroundKey);
	TestEqual(TEXT("Every volume has its own setting"), Keys.Num(), static_cast<int32>(EGroup::Count) + 1);

	// Without a game (no world) nothing is turned down
	TestEqual(TEXT("Full volume without a game instance"), HCSoundVolumes::Of(nullptr, EGroup::Scares), 1.f);
	return true;
}

#endif
