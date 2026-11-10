#include "Core/HCGameSettings.h"

UHCGameSettings::UHCGameSettings()
{
	SectionName = TEXT("HowCouldItBeYou");
	TaskDefinitions = FHCTaskCatalog::MakeDefaults();
}

const FHCTaskDefinition* UHCGameSettings::FindTask(FName TaskId) const
{
	return TaskDefinitions.FindByPredicate([TaskId](const FHCTaskDefinition& Def) { return Def.TaskId == TaskId; });
}
