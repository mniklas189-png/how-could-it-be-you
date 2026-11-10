#include "Core/HCMatchRules.h"

#include "Core/HCLog.h"

UHCMatchRules* UHCMatchRules::Load(UObject* Outer, FName ModeId)
{
	UHCMatchRules* Rules = NewObject<UHCMatchRules>(Outer, ModeId);
	Rules->LoadConfig();
	UE_LOG(LogHCMatch, Log, TEXT("Match rules '%s': %d monster(s), tasks win=%d, timer=%d"),
		*ModeId.ToString(), Rules->NumMonsters, Rules->bCrewWinsByTasks ? 1 : 0, Rules->bTimerEnabled ? 1 : 0);
	return Rules;
}
