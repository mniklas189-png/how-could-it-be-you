#include "Roles/HCJobs.h"

#include "Math/RandomStream.h"

namespace HCJobs
{
	FText Name(EHCJob Job)
	{
		switch (Job)
		{
		case EHCJob::Technician: return NSLOCTEXT("HC", "JobTechnician", "Techniker");
		case EHCJob::Medic: return NSLOCTEXT("HC", "JobMedic", "Sanitäter");
		case EHCJob::Guard: return NSLOCTEXT("HC", "JobGuard", "Wachmann");
		case EHCJob::Radio: return NSLOCTEXT("HC", "JobRadio", "Funker");
		case EHCJob::Biologist: return NSLOCTEXT("HC", "JobBiologist", "Biologe");
		case EHCJob::Commander: return NSLOCTEXT("HC", "JobCommander", "Kommandant");
		default: return FText::GetEmpty();
		}
	}

	FText Ability(EHCJob Job)
	{
		switch (Job)
		{
		case EHCJob::Technician: return NSLOCTEXT("HC", "JobTechnicianAbility", "Kriecht durch die Lüftungsschächte, langsam und laut. Repariert Sicherungen doppelt so schnell.");
		case EHCJob::Medic: return NSLOCTEXT("HC", "JobMedicAbility", "Untersucht Überreste: wie lange tot, und welche Gestalt der Täter trug.");
		case EHCJob::Guard: return NSLOCTEXT("HC", "JobGuardAbility", "Sieht die Kameras auf einem Tablet, ein paar Sekunden zwischen zwei Meetings.");
		case EHCJob::Radio: return NSLOCTEXT("HC", "JobRadioAbility", "Einmal zwischen zwei Meetings: eine Durchsage an alle oder ein Meeting aus der Ferne.");
		case EHCJob::Biologist: return NSLOCTEXT("HC", "JobBiologistAbility", "Sieht frischen Schleim länger und unter UV-Licht, auch wo andere nichts mehr sehen.");
		case EHCJob::Commander: return NSLOCTEXT("HC", "JobCommanderAbility", "Alle wissen, wer es ist. Seine Stimme zählt im Meeting doppelt.");
		default: return FText::GetEmpty();
		}
	}

	const TCHAR* Id(EHCJob Job)
	{
		switch (Job)
		{
		case EHCJob::Technician: return TEXT("Technician");
		case EHCJob::Medic: return TEXT("Medic");
		case EHCJob::Guard: return TEXT("Guard");
		case EHCJob::Radio: return TEXT("Radio");
		case EHCJob::Biologist: return TEXT("Biologist");
		case EHCJob::Commander: return TEXT("Commander");
		default: return TEXT("None");
		}
	}

	EHCJob Parse(const FString& Text)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const EHCJob Job = FromIndex(Index);
			if (Text.Equals(Id(Job), ESearchCase::IgnoreCase) || Text.Equals(Name(Job).ToString(), ESearchCase::IgnoreCase))
			{
				return Job;
			}
		}
		return EHCJob::None;
	}

	bool HasAbilityKey(EHCJob Job)
	{
		return Job != EHCJob::None && Job != EHCJob::Commander;
	}
}

TMap<int32, EHCJob> FHCJobAssigner::Assign(const TArray<int32>& Crew, const TArray<int32>& Monsters, uint8 Mask, int32 Seed, const TMap<int32, EHCJob>& Forced)
{
	TMap<int32, EHCJob> Result;
	FRandomStream Random(Seed);
	auto Shuffle = [&Random](auto& Array)
	{
		for (int32 i = Array.Num() - 1; i > 0; --i)
		{
			Array.Swap(i, Random.RandRange(0, i));
		}
	};

	// Jobs that are still free, in a random order
	TArray<EHCJob> Free;
	for (int32 Index = 0; Index < HCJobs::Count; ++Index)
	{
		const EHCJob Job = HCJobs::FromIndex(Index);
		if (Mask & HCJobs::Bit(Job))
		{
			Free.Add(Job);
		}
	}
	for (const TPair<int32, EHCJob>& Pair : Forced)
	{
		Free.Remove(Pair.Value);
	}
	Shuffle(Free);

	TArray<int32> OpenCrew;
	for (const int32 Id : Crew)
	{
		if (const EHCJob* Job = Forced.Find(Id))
		{
			Result.Add(Id, *Job);
		}
		else
		{
			OpenCrew.Add(Id);
		}
	}
	Shuffle(OpenCrew);
	for (const int32 Id : OpenCrew)
	{
		Result.Add(Id, Free.IsEmpty() ? EHCJob::None : Free.Pop(EAllowShrinking::No));
	}

	// Covers: what nobody of the crew holds; never the commander, whom everybody knows
	Free.Remove(EHCJob::Commander);
	for (const int32 Id : Monsters)
	{
		if (const EHCJob* Job = Forced.Find(Id))
		{
			Result.Add(Id, *Job == EHCJob::Commander ? EHCJob::None : *Job);
			continue;
		}
		Result.Add(Id, Free.IsEmpty() ? EHCJob::None : Free.Pop(EAllowShrinking::No));
	}
	return Result;
}
