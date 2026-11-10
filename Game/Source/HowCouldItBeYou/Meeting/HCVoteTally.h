#pragma once

#include "CoreMinimal.h"

/** Result of counting a vote. */
struct HOWCOULDITBEYOU_API FHCVoteOutcome
{
	/** Votes per candidate; the key FHCVoteTally::SkipId holds "skip" (including everyone who did not vote). */
	TMap<int32, int32> Counts;
	/** Who voted for whom (only valid, counted votes; non-voters appear under SkipId). */
	TMap<int32, TArray<int32>> VotersByTarget;
	/** Player to eject, or INDEX_NONE if nobody is ejected. */
	int32 EjectedId = INDEX_NONE;
};

/**
 * Pure vote counting (docs/02 §8.6), no world needed:
 * 1. Who does not vote counts as "skip".
 * 2. Votes of players who may not vote are ignored; votes for invalid targets count as "skip".
 * 3. The player with the most votes is ejected if he is alone in front AND has more votes than "skip".
 * 4. A tie at the top (between players or with "skip") ejects nobody.
 */
struct HOWCOULDITBEYOU_API FHCVoteTally
{
	static constexpr int32 SkipId = -1;

	/**
	 * @param Voters      everyone who may vote (alive + connected)
	 * @param Candidates  everyone who can be voted for (alive + connected)
	 * @param Votes       voter -> target; missing entry or invalid target = skip
	 */
	static FHCVoteOutcome Count(const TArray<int32>& Voters, const TArray<int32>& Candidates, const TMap<int32, int32>& Votes);
};
