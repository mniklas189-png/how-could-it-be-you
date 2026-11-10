#include "Abilities/HCShapeshiftRules.h"

EHCShapeshiftReject FHCShapeshiftRules::Validate(const FHCShapeshiftCheck& In)
{
	if (!In.bPhasePlaying)
	{
		return EHCShapeshiftReject::NotPlaying;
	}
	if (!In.bIsMonster)
	{
		return EHCShapeshiftReject::NotMonster;
	}
	if (!In.bAlive)
	{
		return EHCShapeshiftReject::Dead;
	}
	if (In.bKillLocked)
	{
		return EHCShapeshiftReject::Locked;
	}
	if (In.State != EHCShapeshiftState::Ready)
	{
		return EHCShapeshiftReject::Busy;
	}
	if (In.Now < In.ReadyTime)
	{
		return EHCShapeshiftReject::OnCooldown;
	}
	if (!In.bTargetValid)
	{
		return EHCShapeshiftReject::TargetInvalid;
	}
	if (In.bSameShape)
	{
		return EHCShapeshiftReject::SameShape;
	}
	return EHCShapeshiftReject::None;
}

bool FHCShapeshiftRules::CanRevert(const FHCShapeshiftCheck& In)
{
	return In.bPhasePlaying && In.bIsMonster && In.bAlive && !In.bKillLocked && In.State == EHCShapeshiftState::Disguised;
}

FHCShapeshiftRules::ETargetGone FHCShapeshiftRules::OnTargetGone(EHCShapeshiftState State, bool bDisconnected, bool bRevertWhenTargetDies)
{
	if (State == EHCShapeshiftState::Transforming)
	{
		return ETargetGone::Cancel;
	}
	if (State == EHCShapeshiftState::Disguised && (bDisconnected || bRevertWhenTargetDies))
	{
		return ETargetGone::Revert;
	}
	return ETargetGone::Nothing;
}

const TCHAR* FHCShapeshiftRules::ToString(EHCShapeshiftReject Reject)
{
	switch (Reject)
	{
	case EHCShapeshiftReject::None: return TEXT("None");
	case EHCShapeshiftReject::NotPlaying: return TEXT("NotPlaying");
	case EHCShapeshiftReject::NotMonster: return TEXT("NotMonster");
	case EHCShapeshiftReject::Dead: return TEXT("Dead");
	case EHCShapeshiftReject::Locked: return TEXT("Locked");
	case EHCShapeshiftReject::Busy: return TEXT("Busy");
	case EHCShapeshiftReject::OnCooldown: return TEXT("OnCooldown");
	case EHCShapeshiftReject::TargetInvalid: return TEXT("TargetInvalid");
	case EHCShapeshiftReject::SameShape: return TEXT("SameShape");
	default: return TEXT("?");
	}
}
