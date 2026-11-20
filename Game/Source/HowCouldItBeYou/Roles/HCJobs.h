#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"

/**
 * The jobs of the crew (lobby option "Berufe"). Every crew member gets one of the jobs the host switched on, each job
 * at most once; who is left over has none. The Wandler gets a job as its cover: one that nobody of the crew holds,
 * never the commander. The cover shows on its role card and can be claimed, but its ability does nothing.
 * The commander is known to everybody (its vote counts twice); every other job is secret.
 */
namespace HCJobs
{
	/** Jobs without None. */
	constexpr int32 Count = 6;
	/** Every job switched on. */
	constexpr uint8 AllMask = 0x7E;

	constexpr uint8 Bit(EHCJob Job) { return static_cast<uint8>(1u << static_cast<uint8>(Job)); }
	constexpr EHCJob FromIndex(int32 Index) { return static_cast<EHCJob>(FMath::Clamp(Index, 0, Count - 1) + 1); }
	/** "Techniker" ... (empty for None). */
	HOWCOULDITBEYOU_API FText Name(EHCJob Job);
	/** What the job can do, one line for the role card and the lobby. */
	HOWCOULDITBEYOU_API FText Ability(EHCJob Job);
	/** Id for log lines and dev flags ("Technician", "Medic" ...). */
	HOWCOULDITBEYOU_API const TCHAR* Id(EHCJob Job);
	/** By id or German name, case does not matter; None if unknown. */
	HOWCOULDITBEYOU_API EHCJob Parse(const FString& Text);
	/** Does the job have an ability on the ability key (all but the commander)? */
	HOWCOULDITBEYOU_API bool HasAbilityKey(EHCJob Job);
}

/** Pure job assignment, deterministic for a seed (unit-tested). */
struct HOWCOULDITBEYOU_API FHCJobAssigner
{
	/**
	 * @param Crew      ids of the crew members
	 * @param Monsters  ids of the Wandler(s): they get covers
	 * @param Mask      jobs switched on (HCJobs::Bit)
	 * @param Forced    from dev flags, take priority (for crew: the real job; for a Wandler: the cover)
	 */
	static TMap<int32, EHCJob> Assign(const TArray<int32>& Crew, const TArray<int32>& Monsters, uint8 Mask, int32 Seed, const TMap<int32, EHCJob>& Forced = {});
};
