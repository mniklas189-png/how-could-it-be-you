#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"

/** Why the server refused a shapeshift request (docs/02 §6.3). */
enum class EHCShapeshiftReject : uint8
{
	None,
	NotPlaying,
	NotMonster,
	Dead,
	/** In a kill lock. */
	Locked,
	/** Already transforming, disguised or reverting. */
	Busy,
	OnCooldown,
	/** Target is missing, disconnected, dead or the monster itself. */
	TargetInvalid,
	/** Target is the shape the monster already wears. */
	SameShape
};

/** Snapshot of everything the decision depends on, so the rules can be unit-tested without a world. */
struct HOWCOULDITBEYOU_API FHCShapeshiftCheck
{
	bool bPhasePlaying = true;
	bool bIsMonster = true;
	bool bAlive = true;
	bool bKillLocked = false;
	EHCShapeshiftState State = EHCShapeshiftState::Ready;
	double Now = 0.0;
	/** Server time from which the next shapeshift is allowed. */
	double ReadyTime = 0.0;
	/** Another connected, living player. */
	bool bTargetValid = true;
	bool bSameShape = false;
};

struct HOWCOULDITBEYOU_API FHCShapeshiftRules
{
	/** May the monster start turning into the target? */
	static EHCShapeshiftReject Validate(const FHCShapeshiftCheck& In);

	/** May the monster end its disguise early? Only while disguised and in play. */
	static bool CanRevert(const FHCShapeshiftCheck& In);

	/**
	 * What happens to a shapeshift when its target dies or disconnects (docs/02 §6.6):
	 * during the transformation it is cancelled without cooldown; a finished disguise ends early only when the
	 * target disconnected or the lobby option says so.
	 */
	enum class ETargetGone : uint8 { Nothing, Cancel, Revert };
	static ETargetGone OnTargetGone(EHCShapeshiftState State, bool bDisconnected, bool bRevertWhenTargetDies);

	static const TCHAR* ToString(EHCShapeshiftReject Reject);
};
