#include "Abilities/HCKillRules.h"

EHCKillReject FHCKillRules::Validate(const FHCKillCheck& In)
{
	if (!In.bPhasePlaying)
	{
		return EHCKillReject::NotPlaying;
	}
	if (!In.bAttackerIsMonster)
	{
		return EHCKillReject::NotMonster;
	}
	if (!In.bAttackerAlive)
	{
		return EHCKillReject::AttackerDead;
	}
	if (In.bAttackerLocked)
	{
		return EHCKillReject::AttackerLocked;
	}
	// Only living crew can be killed
	if (!In.bTargetAlive || In.bTargetIsMonster)
	{
		return EHCKillReject::TargetInvalid;
	}
	if (In.bTargetLocked)
	{
		return EHCKillReject::TargetLocked;
	}
	if (In.Now < In.KillReadyTime)
	{
		return EHCKillReject::OnCooldown;
	}
	// The meeting room is kill-free for both sides (docs/01 §4.2)
	if (In.bAttackerInMeetingRoom || In.bTargetInMeetingRoom)
	{
		return EHCKillReject::InMeetingRoom;
	}
	if (In.Distance > In.KillRange + In.RangeTolerance)
	{
		return EHCKillReject::TooFar;
	}
	if (!In.bLineOfSight)
	{
		return EHCKillReject::NoLineOfSight;
	}
	return EHCKillReject::None;
}

const TCHAR* FHCKillRules::ToString(EHCKillReject Reject)
{
	switch (Reject)
	{
	case EHCKillReject::None:           return TEXT("ok");
	case EHCKillReject::NotPlaying:     return TEXT("not in phase Playing");
	case EHCKillReject::NotMonster:     return TEXT("attacker is not the monster");
	case EHCKillReject::AttackerDead:   return TEXT("attacker is dead");
	case EHCKillReject::AttackerLocked: return TEXT("attacker is locked");
	case EHCKillReject::TargetInvalid:  return TEXT("target is not living crew");
	case EHCKillReject::TargetLocked:   return TEXT("target is already being killed");
	case EHCKillReject::OnCooldown:     return TEXT("kill is on cooldown");
	case EHCKillReject::TooFar:         return TEXT("target is too far away");
	case EHCKillReject::NoLineOfSight:  return TEXT("no line of sight");
	case EHCKillReject::InMeetingRoom:  return TEXT("meeting room is kill-free");
	}
	return TEXT("?");
}
