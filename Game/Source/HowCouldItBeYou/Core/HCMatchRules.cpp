#include "Core/HCMatchRules.h"

#include "Core/HCLog.h"
#include "Misc/CommandLine.h"

UHCMatchRules* UHCMatchRules::Load(UObject* Outer, FName ModeId)
{
	UHCMatchRules* Rules = NewObject<UHCMatchRules>(Outer, ModeId);
	Rules->LoadConfig();
#if !UE_BUILD_SHIPPING
	// Test rounds: -HCDiscussion=<s> and -HCVoting=<s> shorten the meeting
	FParse::Value(FCommandLine::Get(), TEXT("HCDiscussion="), Rules->DiscussionTime);
	FParse::Value(FCommandLine::Get(), TEXT("HCVoting="), Rules->VotingTime);
	FParse::Value(FCommandLine::Get(), TEXT("HCShiftDuration="), Rules->ShapeshiftDuration);
	FParse::Value(FCommandLine::Get(), TEXT("HCShiftCooldown="), Rules->ShapeshiftCooldown);
	FParse::Value(FCommandLine::Get(), TEXT("HCTrueFormDuration="), Rules->TrueFormDuration);
	FParse::Value(FCommandLine::Get(), TEXT("HCSabotageDelay="), Rules->SabotageStartDelay);
	FParse::Value(FCommandLine::Get(), TEXT("HCOutageMax="), Rules->PowerOutageMaxDuration);
	FParse::Value(FCommandLine::Get(), TEXT("HCSabotageCooldown="), Rules->PowerSabotageCooldown);
	FParse::Value(FCommandLine::Get(), TEXT("HCSabotageCooldown="), Rules->DoorSabotageCooldown);
#endif
	UE_LOG(LogHCMatch, Log, TEXT("Match rules '%s': %d monster(s), tasks win=%d, timer=%d"),
		*ModeId.ToString(), Rules->NumMonsters, Rules->bCrewWinsByTasks ? 1 : 0, Rules->bTimerEnabled ? 1 : 0);
	return Rules;
}
