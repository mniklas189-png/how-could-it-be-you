#include "Roles/HCJobManager.h"

#include "Abilities/HCRemains.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Core/HCText.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Meeting/HCMeetingManager.h"
#include "Roles/HCJobs.h"
#include "TimerManager.h"
#include "World/HCMonitorDevices.h"
#include "World/HCStationBuilder.h"

AHCJobManager::AHCJobManager()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
}

void AHCJobManager::Init(AHCGameMode* InGameMode)
{
	GameMode = InGameMode;
	GetWorldTimerManager().SetTimer(TickTimer, this, &AHCJobManager::TickJobs, 0.25f, true);
	LastTick = GetNow();
}

double AHCJobManager::GetNow() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.0;
}

bool AHCJobManager::HasJob(const AHCPlayerState* PS, EHCJob Job)
{
	return PS && PS->IsAlive() && PS->GetRoleInfo().Role == EHCRole::Crew && PS->GetRoleInfo().Job == Job && Job != EHCJob::None;
}

bool AHCJobManager::HasJob(const AHCCharacter* Who, EHCJob Job)
{
	return Who && HasJob(Who->GetPlayerState<AHCPlayerState>(), Job);
}

void AHCJobManager::Notice(AHCPlayerController* PC, const FText& Text, float Seconds) const
{
	if (PC)
	{
		PC->ClientShowNotice(Text, Seconds);
	}
}

void AHCJobManager::OnPlayResumed()
{
	if (!GameMode)
	{
		return;
	}
	for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
	{
		FHCJobState& State = PS->ServerJobState();
		State = FHCJobState();
		if (HasJob(PS, EHCJob::Radio))
		{
			State.Charges = 1;
		}
		if (HasJob(PS, EHCJob::Guard))
		{
			State.TabletSeconds = TabletSeconds;
		}
		PS->ForceNetUpdate();
	}
}

void AHCJobManager::OnMeetingStart()
{
	for (const FExamination& Exam : Examinations)
	{
		if (AHCPlayerState* PS = Exam.Who.Get())
		{
			PS->ServerJobState().ExamineEnd = 0.0;
		}
	}
	Examinations.Reset();
	// The camera desk lets every viewer go when the phase changes; nothing more to do for the tablets
	Tablets.Reset();
}

void AHCJobManager::UseAbility(AHCPlayerController* PC, EHCAbilityAction Action, const FString& Text)
{
	AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	AHCCharacter* Who = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	if (!GameMode || !PS || !Who || GameMode->GetPhase() != EHCMatchPhase::Playing || !Who->CanAct())
	{
		return;
	}
	switch (Action)
	{
	case EHCAbilityAction::Use:
		if (HasJob(PS, EHCJob::Medic))
		{
			StartExamination(PC, PS, Who);
		}
		else if (HasJob(PS, EHCJob::Guard))
		{
			OpenTablet(PC, PS, Who);
		}
		else if (HasJob(PS, EHCJob::Biologist))
		{
			ToggleUV(PC, Who);
		}
		break;
	case EHCAbilityAction::RadioAnnounce:
		if (HasJob(PS, EHCJob::Radio))
		{
			Announce(PC, PS, Who, Text);
		}
		break;
	case EHCAbilityAction::RadioMeeting:
		if (HasJob(PS, EHCJob::Radio))
		{
			RemoteMeeting(PC, PS, Who);
		}
		break;
	}
}

// ---------------------------------------------------------------------------
// Medic

void AHCJobManager::StartExamination(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who)
{
	if (Examinations.ContainsByPredicate([PS](const FExamination& Exam) { return Exam.Who.Get() == PS; }))
	{
		return;
	}
	AHCRemains* Best = nullptr;
	double BestDistance = FMath::Square(ExamineRange);
	for (TActorIterator<AHCRemains> It(GetWorld()); It; ++It)
	{
		const double Distance = FVector::DistSquared2D(It->GetActorLocation(), Who->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	if (!Best)
	{
		Notice(PC, NSLOCTEXT("HC", "MedicNoRemains", "Hier gibt es nichts zu untersuchen. Geh näher an Überreste heran."), 4.f);
		return;
	}
	FExamination Exam;
	Exam.Who = PS;
	Exam.Character = Who;
	Exam.Remains = Best;
	Exam.Start = Who->GetActorLocation();
	Exam.End = GetNow() + ExamineSeconds;
	Examinations.Add(Exam);
	PS->ServerJobState().ExamineEnd = Exam.End;
	PS->ForceNetUpdate();
	UE_LOG(LogHCMatch, Log, TEXT("Job: medic %s examines the remains of %s"), *PS->GetPlayerName(), *Best->GetVictimName());
}

void AHCJobManager::FinishExamination(int32 Index)
{
	const FExamination Exam = Examinations[Index];
	Examinations.RemoveAt(Index);
	AHCPlayerState* PS = Exam.Who.Get();
	AHCRemains* Remains = Exam.Remains.Get();
	if (!PS)
	{
		return;
	}
	PS->ServerJobState().ExamineEnd = 0.0;
	PS->ForceNetUpdate();
	AHCPlayerController* PC = Cast<AHCPlayerController>(PS->GetOwningController());
	if (!Remains)
	{
		return;
	}
	const int32 Seconds = FMath::Max(1, FMath::RoundToInt32(GetNow() - Remains->GetDeathTime()));
	FText Killer;
	if (Remains->WasKillerInTrueForm())
	{
		Killer = NSLOCTEXT("HC", "MedicKillerTrue", "Der Täter zeigte seine wahre Gestalt.");
	}
	else if (HCIdentity::IsValid(Remains->GetKillerIdentity()))
	{
		Killer = FText::Format(NSLOCTEXT("HC", "MedicKillerFmt", "Der Täter trug die Gestalt von {0} ({1} · {2})."), FText::FromString(Remains->GetKillerName()),
			HCIdentity::ColorName(Remains->GetKillerIdentity()), HCIdentity::SymbolName(Remains->GetKillerIdentity()));
	}
	else
	{
		Killer = NSLOCTEXT("HC", "MedicKillerUnknown", "Wer es war, verrät die Wunde nicht.");
	}
	Notice(PC, FText::Format(NSLOCTEXT("HC", "MedicResultFmt", "BEFUND: {0} ist seit {1} s tot. {2}"), FText::FromString(Remains->GetVictimName()), Seconds, Killer), 10.f);
	UE_LOG(LogHCMatch, Log, TEXT("Job: medic %s finished: %s dead for %d s, killer looked like %s%s"), *PS->GetPlayerName(), *Remains->GetVictimName(), Seconds,
		*Remains->GetKillerName(), Remains->WasKillerInTrueForm() ? TEXT(" (true form)") : TEXT(""));
}

// ---------------------------------------------------------------------------
// Guard

void AHCJobManager::OpenTablet(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who)
{
	if (PS->GetJobState().TabletSeconds < 0.5f)
	{
		Notice(PC, NSLOCTEXT("HC", "GuardEmpty", "Der Tablet-Akku ist leer. Nach dem nächsten Meeting ist er wieder voll."), 4.f);
		return;
	}
	TActorIterator<AHCSecurityConsole> It(GetWorld());
	AHCSecurityConsole* Console = It ? *It : nullptr;
	if (!Console)
	{
		Notice(PC, NSLOCTEXT("HC", "GuardNoCameras", "Auf dieser Station gibt es keine Kameras."), 4.f);
		return;
	}
	if (!Console->IsPowered())
	{
		Notice(PC, NSLOCTEXT("HC", "GuardNoPower", "Kein Signal: Die Kameras haben keinen Strom."), 4.f);
		return;
	}
	if (Console->HasViewer(Who))
	{
		return;
	}
	Console->ServerAddRemoteViewer(Who);
	Tablets.Add({ PS, Who, Console });
	UE_LOG(LogHCMatch, Log, TEXT("Job: guard %s opens the camera tablet (%.0f s left)"), *PS->GetPlayerName(), PS->GetJobState().TabletSeconds);
}

// ---------------------------------------------------------------------------
// Biologist

void AHCJobManager::ToggleUV(AHCPlayerController* PC, AHCCharacter* Who)
{
	const bool bOn = !Who->IsUVLight();
	Who->ServerSetUVLight(bOn);
	if (bOn && !Who->IsFlashlightOn())
	{
		Who->ServerSetFlashlight(true);
	}
}

// ---------------------------------------------------------------------------
// Radio operator

void AHCJobManager::Announce(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who, const FString& Text)
{
	FString Clean = Text.Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" ")).TrimStartAndEnd();
	if (Clean.Len() > 80)
	{
		Clean = Clean.Left(80);
	}
	if (Clean.IsEmpty() || PS->GetJobState().Charges == 0)
	{
		return;
	}
	PS->ServerJobState().Charges = 0;
	PS->ForceNetUpdate();
	FText Room = NSLOCTEXT("HC", "RadioSomewhere", "irgendwo");
	TActorIterator<AHCStationBuilder> Builder(GetWorld());
	if (Builder)
	{
		const FName Space = Builder->GetSpaceNameAt(Who->GetActorLocation());
		if (!Space.IsNone())
		{
			Room = HCText::Room(Space);
		}
	}
	if (AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>())
	{
		GS->MulticastAnnouncement(FText::Format(NSLOCTEXT("HC", "RadioAnnouncementFmt", "FUNKSPRUCH VON {0} ({1}): „{2}“"),
			FText::FromString(PS->GetPlayerName()), Room, FText::FromString(Clean)), 10.f);
	}
	UE_LOG(LogHCMatch, Log, TEXT("Job: radio %s announces: %s"), *PS->GetPlayerName(), *Clean);
}

void AHCJobManager::RemoteMeeting(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who)
{
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if (PS->GetJobState().Charges == 0)
	{
		return;
	}
	if (PS->GetEmergencyMeetingsLeft() <= 0)
	{
		Notice(PC, NSLOCTEXT("HC", "RadioNoMeetings", "Du hast dein Notfall-Meeting schon verbraucht."), 4.f);
		return;
	}
	if (GS && GS->GetCrisis() != EHCCrisis::None)
	{
		Notice(PC, NSLOCTEXT("HC", "RadioCrisis", "Während eines Notfalls geht kein Meeting. Erst beheben!"), 4.f);
		return;
	}
	if (GetNow() < GameMode->GetMeeting()->GetEmergencyReadyTime())
	{
		Notice(PC, NSLOCTEXT("HC", "RadioTooSoon", "Das Funknetz ist noch gesperrt. Gleich noch einmal versuchen."), 4.f);
		return;
	}
	if (GameMode->GetMeeting()->RequestMeeting(PS, FString()))
	{
		PS->ServerSetEmergencyMeetingsLeft(PS->GetEmergencyMeetingsLeft() - 1);
		PS->ServerJobState().Charges = 0;
		UE_LOG(LogHCMatch, Log, TEXT("Job: radio %s calls a meeting from afar"), *PS->GetPlayerName());
	}
}

// ---------------------------------------------------------------------------

void AHCJobManager::TickJobs()
{
	const double Now = GetNow();
	const float Step = static_cast<float>(FMath::Clamp(Now - LastTick, 0.0, 1.0));
	LastTick = Now;
	const bool bPlaying = GameMode && GameMode->GetPhase() == EHCMatchPhase::Playing;

	for (int32 Index = Examinations.Num() - 1; Index >= 0; --Index)
	{
		const FExamination& Exam = Examinations[Index];
		AHCPlayerState* PS = Exam.Who.Get();
		const AHCCharacter* Who = Exam.Character.Get();
		const bool bMoved = !Who || FVector::DistSquared2D(Who->GetActorLocation(), Exam.Start) > FMath::Square(80.f);
		if (!PS || !bPlaying || !HasJob(PS, EHCJob::Medic) || bMoved || !Who->CanAct() || !Exam.Remains.IsValid())
		{
			if (PS)
			{
				PS->ServerJobState().ExamineEnd = 0.0;
				PS->ForceNetUpdate();
				Notice(Cast<AHCPlayerController>(PS->GetOwningController()), NSLOCTEXT("HC", "MedicAborted", "Untersuchung abgebrochen."), 3.f);
			}
			Examinations.RemoveAt(Index);
			continue;
		}
		if (Now >= Exam.End)
		{
			FinishExamination(Index);
		}
	}

	// The tablet runs down while the guard watches; when it is empty the picture goes
	for (int32 Index = Tablets.Num() - 1; Index >= 0; --Index)
	{
		const FTablet& Tablet = Tablets[Index];
		AHCPlayerState* PS = Tablet.Who.Get();
		AHCCharacter* Who = Tablet.Character.Get();
		AHCSecurityConsole* Console = Tablet.Console.Get();
		if (!PS || !Who || !Console || !Console->HasViewer(Who))
		{
			Tablets.RemoveAt(Index);
			continue;
		}
		FHCJobState& State = PS->ServerJobState();
		State.TabletSeconds = FMath::Max(0.f, State.TabletSeconds - Step);
		PS->ForceNetUpdate();
		if (State.TabletSeconds <= 0.f || !bPlaying)
		{
			Console->ServerRemoveViewer(Who, true);
			Tablets.RemoveAt(Index);
		}
	}
}
