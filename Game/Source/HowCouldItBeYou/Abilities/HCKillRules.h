#pragma once

#include "CoreMinimal.h"

/** Why the server refused a kill request (docs/02 §7.1). */
enum class EHCKillReject : uint8
{
	None,
	NotPlaying,
	NotMonster,
	AttackerDead,
	AttackerLocked,
	TargetInvalid,
	TargetLocked,
	OnCooldown,
	TooFar,
	NoLineOfSight,
	InMeetingRoom
};

/** Snapshot of everything the kill decision depends on, so the rules can be unit-tested without a world. */
struct HOWCOULDITBEYOU_API FHCKillCheck
{
	bool bPhasePlaying = true;
	bool bAttackerIsMonster = true;
	bool bAttackerAlive = true;
	/** Attacker is in a kill lock already. */
	bool bAttackerLocked = false;
	/** Target exists, is connected and alive. */
	bool bTargetAlive = true;
	bool bTargetIsMonster = false;
	bool bTargetLocked = false;
	double Now = 0.0;
	/** Server time from which the next kill is allowed (cooldown and first-kill delay). */
	double KillReadyTime = 0.0;
	float Distance = 0.f;
	float KillRange = 200.f;
	/** Extra distance the server allows for ping (planning value 50 cm). */
	float RangeTolerance = 50.f;
	bool bLineOfSight = true;
	bool bAttackerInMeetingRoom = false;
	bool bTargetInMeetingRoom = false;
};

struct HOWCOULDITBEYOU_API FHCKillRules
{
	static EHCKillReject Validate(const FHCKillCheck& In);
	static const TCHAR* ToString(EHCKillReject Reject);
};
