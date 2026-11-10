#include "Meeting/HCVoteTally.h"

FHCVoteOutcome FHCVoteTally::Count(const TArray<int32>& Voters, const TArray<int32>& Candidates, const TMap<int32, int32>& Votes)
{
	FHCVoteOutcome Outcome;
	Outcome.Counts.Add(SkipId, 0);
	for (const int32 Candidate : Candidates)
	{
		Outcome.Counts.FindOrAdd(Candidate, 0);
	}

	// Only entitled voters count, each exactly once; anything invalid becomes a skip
	TSet<int32> Seen;
	for (const int32 Voter : Voters)
	{
		if (Seen.Contains(Voter))
		{
			continue;
		}
		Seen.Add(Voter);
		int32 Target = SkipId;
		if (const int32* Chosen = Votes.Find(Voter))
		{
			if (*Chosen != SkipId && Candidates.Contains(*Chosen))
			{
				Target = *Chosen;
			}
		}
		Outcome.Counts.FindOrAdd(Target, 0)++;
		Outcome.VotersByTarget.FindOrAdd(Target).Add(Voter);
	}

	// Relative majority: alone in front and more votes than "skip"
	int32 Best = INDEX_NONE;
	int32 BestCount = 0;
	bool bTie = false;
	for (const TPair<int32, int32>& Pair : Outcome.Counts)
	{
		if (Pair.Key == SkipId)
		{
			continue;
		}
		if (Pair.Value > BestCount)
		{
			Best = Pair.Key;
			BestCount = Pair.Value;
			bTie = false;
		}
		else if (Pair.Value == BestCount && BestCount > 0)
		{
			bTie = true;
		}
	}
	if (Best != INDEX_NONE && !bTie && BestCount > Outcome.Counts[SkipId])
	{
		Outcome.EjectedId = Best;
	}
	return Outcome;
}
