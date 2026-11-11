#include "Sabotage/HCSabotageRules.h"

EHCSabotageReject FHCSabotageRules::Validate(const FHCSabotageCheck& In)
{
	if (!In.bPhasePlaying)
	{
		return EHCSabotageReject::NotPlaying;
	}
	if (!In.bIsMonster)
	{
		return EHCSabotageReject::NotMonster;
	}
	if (!In.bAlive)
	{
		return EHCSabotageReject::Dead;
	}
	// Never the command center: neither its power nor its doors (docs/01 §7 rule 2)
	if (!In.bTargetValid || In.bCommandCenter)
	{
		return EHCSabotageReject::TargetInvalid;
	}
	if (In.Now < In.EarliestTime)
	{
		return EHCSabotageReject::TooEarly;
	}
	if (In.bTypeActive)
	{
		return EHCSabotageReject::AlreadyActive;
	}
	if (In.Now < In.ReadyTime)
	{
		return EHCSabotageReject::OnCooldown;
	}
	if (!In.bPathStaysOpen)
	{
		return EHCSabotageReject::WouldCutOff;
	}
	return EHCSabotageReject::None;
}

const TCHAR* FHCSabotageRules::ToString(EHCSabotageReject Reject)
{
	switch (Reject)
	{
	case EHCSabotageReject::None: return TEXT("None");
	case EHCSabotageReject::NotPlaying: return TEXT("NotPlaying");
	case EHCSabotageReject::NotMonster: return TEXT("NotMonster");
	case EHCSabotageReject::Dead: return TEXT("Dead");
	case EHCSabotageReject::TooEarly: return TEXT("TooEarly");
	case EHCSabotageReject::OnCooldown: return TEXT("OnCooldown");
	case EHCSabotageReject::AlreadyActive: return TEXT("AlreadyActive");
	case EHCSabotageReject::TargetInvalid: return TEXT("TargetInvalid");
	case EHCSabotageReject::WouldCutOff: return TEXT("WouldCutOff");
	default: return TEXT("?");
	}
}
