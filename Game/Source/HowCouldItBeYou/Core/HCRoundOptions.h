#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * Lobby options of the host that change how a round is played: ventilation shafts, a second Wandler from eight
 * players, jobs and which of them. Remembered in the host's settings (UHCGameInstance options) like the voice chat rule,
 * so they survive the travel to the round and back; the game state carries them to everybody (AHCGameState).
 * Development flags override them for test rounds without saving: -HCVents=0|1, -HCSecondMonster=0|1, -HCJobs=0|1,
 * -HCJobMask=<bits> (bit 1 technician ... bit 6 commander).
 */
namespace HCRoundOptions
{
	/** Option keys in the game instance. */
	HOWCOULDITBEYOU_API extern const TCHAR* const VentsKey;
	HOWCOULDITBEYOU_API extern const TCHAR* const SecondMonsterKey;
	HOWCOULDITBEYOU_API extern const TCHAR* const JobsKey;
	HOWCOULDITBEYOU_API extern const TCHAR* const JobMaskKey;

	/** From eight players on the round may have a second Wandler (when the host switched it on). */
	constexpr int32 SecondMonsterPlayers = 8;

	/** The host's choice on this machine (server): defaults are vents on, second Wandler on, jobs on, every job. */
	HOWCOULDITBEYOU_API bool AreVentsEnabled(const UObject* WorldContext);
	HOWCOULDITBEYOU_API bool IsSecondMonsterEnabled(const UObject* WorldContext);
	HOWCOULDITBEYOU_API bool AreJobsEnabled(const UObject* WorldContext);
	HOWCOULDITBEYOU_API uint8 GetJobMask(const UObject* WorldContext);

	/** Wandlers of a round: the mode's number plus one with the option and enough players (-HCMonsters=N overrides). */
	HOWCOULDITBEYOU_API int32 MonstersFor(int32 BaseMonsters, int32 Players, bool bSecondMonster);
}
