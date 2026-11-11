#pragma once

#include "CoreMinimal.h"

/** Sabotage types of v1 (docs/02 §11.1). */
enum class EHCSabotageType : uint8
{
	PowerOutage,
	DoorLock
};

/** Why the server refused a sabotage request. */
enum class EHCSabotageReject : uint8
{
	None,
	NotPlaying,
	NotMonster,
	Dead,
	/** Less than the start delay after round start or the end of a meeting. */
	TooEarly,
	OnCooldown,
	/** One sabotage of this type is already running. */
	AlreadyActive,
	/** Unknown target, not sabotageable, or it belongs to the command center. */
	TargetInvalid,
	/** Locking this door would cut a room off from the command center. */
	WouldCutOff
};

/** Snapshot of everything the decision depends on, so the rules can be unit-tested without a world. */
struct HOWCOULDITBEYOU_API FHCSabotageCheck
{
	bool bPhasePlaying = true;
	bool bIsMonster = true;
	bool bAlive = true;
	double Now = 0.0;
	/** Round start or end of the last meeting plus the start delay. */
	double EarliestTime = 0.0;
	/** End of this type's cooldown. */
	double ReadyTime = 0.0;
	bool bTypeActive = false;
	bool bTargetValid = true;
	bool bCommandCenter = false;
	bool bPathStaysOpen = true;
};

struct HOWCOULDITBEYOU_API FHCSabotageRules
{
	static EHCSabotageReject Validate(const FHCSabotageCheck& In);
	static const TCHAR* ToString(EHCSabotageReject Reject);
};
