#include "Meeting/HCMeetingManager.h"

#include "Abilities/HCKillComponent.h"
#include "Abilities/HCRemains.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCMatchRules.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Meeting/HCVoteTally.h"
#include "Sabotage/HCSabotageManager.h"
#include "Tasks/HCTaskComponent.h"
#include "TimerManager.h"
#include "World/HCStationBuilder.h"

AHCMeetingManager::AHCMeetingManager()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
}

void AHCMeetingManager::Init(AHCGameMode* InGameMode)
{
	GameMode = InGameMode;
}

AHCGameState* AHCMeetingManager::GetHCGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
}

TArray<AHCPlayerState*> AHCMeetingManager::GetEligible() const
{
	TArray<AHCPlayerState*> Result;
	if (GameMode)
	{
		for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
		{
			if (PS->IsAlive())
			{
				Result.Add(PS);
			}
		}
	}
	return Result;
}

AHCPlayerState* AHCMeetingManager::FindPlayer(int32 PlayerId) const
{
	if (GameMode)
	{
		for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
		{
			if (PS->GetPlayerId() == PlayerId)
			{
				return PS;
			}
		}
	}
	return nullptr;
}

bool AHCMeetingManager::HaveAllVoted() const
{
	const TArray<AHCPlayerState*> Eligible = GetEligible();
	for (const AHCPlayerState* PS : Eligible)
	{
		if (!Votes.Contains(PS->GetPlayerId()))
		{
			return false;
		}
	}
	return Eligible.Num() > 0;
}

void AHCMeetingManager::ResetForRound()
{
	Abort();
	EmergencyReadyTime = 0.0;
	EjectedId = INDEX_NONE;
	MeetingNumber = 0;
	LastChatTime.Reset();
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetEmergencyReadyTime(0.0);
		GS->ServerSetVoteResults({}, INDEX_NONE, FText::GetEmpty());
	}
	if (GameMode)
	{
		const int32 Uses = GameMode->Rules ? GameMode->Rules->EmergencyMeetingsPerPlayer : 1;
		for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
		{
			PS->ServerSetEmergencyMeetingsLeft(Uses);
			PS->ServerSetHasVoted(false);
		}
	}
}

void AHCMeetingManager::Abort()
{
	GetWorldTimerManager().ClearTimer(StageTimer);
	GetWorldTimerManager().ClearTimer(WaitTimer);
	bPending = false;
	Votes.Reset();
	if (Stage != EHCMeetingStage::None)
	{
		Stage = EHCMeetingStage::None;
		if (AHCGameState* GS = GetHCGameState())
		{
			GS->ServerSetMeetingStage(EHCMeetingStage::None);
		}
	}
}

// ---------------------------------------------------------------------------
// Trigger

bool AHCMeetingManager::RequestMeeting(const AHCPlayerState* Caller, const FString& InVictimName)
{
	if (!GameMode || GameMode->GetPhase() != EHCMatchPhase::Playing || IsBusy())
	{
		return false;
	}
	bPending = true;
	PendingSince = GetWorld()->GetTimeSeconds();
	CallerId = Caller ? Caller->GetPlayerId() : INDEX_NONE;
	CallerName = Caller ? Caller->GetPlayerName() : FString(TEXT("?"));
	VictimName = InVictimName;
	UE_LOG(LogHCMatch, Log, TEXT("Meeting: called by %s (%s)"), *CallerName,
		VictimName.IsEmpty() ? TEXT("emergency button") : *FString::Printf(TEXT("remains of %s"), *VictimName));

	// A running kill finishes first, so nobody sits in a meeting half dead (docs/02 §8.1)
	if (GameMode->PendingKills.IsEmpty())
	{
		BeginGathering();
	}
	else
	{
		GetWorldTimerManager().SetTimer(WaitTimer, this, &AHCMeetingManager::TickWaitForKills, 0.1f, true);
	}
	return true;
}

void AHCMeetingManager::TickWaitForKills()
{
	if (!bPending || !GameMode || GameMode->GetPhase() != EHCMatchPhase::Playing)
	{
		// The kill ended the match meanwhile
		GetWorldTimerManager().ClearTimer(WaitTimer);
		bPending = false;
		return;
	}
	const bool bTimeout = GetWorld()->GetTimeSeconds() - PendingSince >= 3.0;
	if (GameMode->PendingKills.IsEmpty() || bTimeout)
	{
		GetWorldTimerManager().ClearTimer(WaitTimer);
		if (bTimeout && !GameMode->PendingKills.IsEmpty())
		{
			UE_LOG(LogHCMatch, Warning, TEXT("HC_FAILSAFE: meeting starts although a kill is still running"));
		}
		BeginGathering();
	}
}

// ---------------------------------------------------------------------------
// Stages

void AHCMeetingManager::SetStage(EHCMeetingStage NewStage, float Duration)
{
	Stage = NewStage;
	GetWorldTimerManager().ClearTimer(StageTimer);
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetMeetingStage(NewStage);
		GS->ServerSetPhaseEnd(Duration);
	}
	if (NewStage != EHCMeetingStage::None && Duration > 0.f)
	{
		GetWorldTimerManager().SetTimer(StageTimer, this, &AHCMeetingManager::OnStageTimer, Duration, false);
	}
}

void AHCMeetingManager::OnStageTimer()
{
	const UHCMatchRules* Rules = GameMode ? GameMode->Rules.Get() : nullptr;
	switch (Stage)
	{
	case EHCMeetingStage::Gathering:
		SetStage(EHCMeetingStage::Discussion, Rules ? Rules->DiscussionTime : 60.f);
		break;
	case EHCMeetingStage::Discussion:
		BeginVoting();
		break;
	case EHCMeetingStage::Voting:
		BeginResults();
		break;
	case EHCMeetingStage::Results:
		AfterResults();
		break;
	default:
		break;
	}
}

void AHCMeetingManager::BeginGathering()
{
	bPending = false;
	Votes.Reset();
	EjectedId = INDEX_NONE;
	const UHCMatchRules* Rules = GameMode->Rules;
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetMeetingInfo(CallerId, CallerName, VictimName);
		GS->ServerSetVoteResults({}, INDEX_NONE, FText::GetEmpty());
	}
	GameMode->SetPhase(EHCMatchPhase::Meeting, 0.f);
	SetStage(EHCMeetingStage::Gathering, Rules ? Rules->GatheringTime : 3.f);
	// The chat of a round goes on from meeting to meeting; a divider line marks where this one starts
	++MeetingNumber;
	DeliverChatLine(FHCChatLine());

	for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
	{
		// Running tasks end; the overlays close on the clients with the phase (docs/02 §8.2)
		UHCTaskComponent* Tasks = PS->GetTaskComponent();
		Tasks->ServerCancelTimedStep();
		PS->ServerSetHasVoted(false);
		// "Ghost tasks do not count": open tasks of the dead leave the total now (docs/02 §9.6)
		if (Rules && !Rules->bGhostTasksCount && !PS->IsAlive() && !Tasks->IsFakeList() && Tasks->CountOpen() > 0)
		{
			GameMode->TrueTotalTasks = FMath::Max(GameMode->TrueCompletedTasks, GameMode->TrueTotalTasks - Tasks->CountOpen());
			Tasks->ServerClear();
		}
	}
	// All remains disappear with the meeting (docs/02 §7.9)
	for (TActorIterator<AHCRemains> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	// A meeting lifts every door lock; a power outage stays (docs/01 §7 rule 5)
	GameMode->GetSabotage()->OnMeetingStart();
	// Every disguise ends at once and without effect: in the meeting everyone is themselves (docs/02 §6.6)
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		It->GetShapeshift()->ServerEndForMeeting();
	}
	SeatPlayers();
	GameMode->PublishTaskProgress();
}

void AHCMeetingManager::SeatPlayers()
{
	const AHCStationBuilder* Builder = GameMode->FindBuilder();
	const TArray<FTransform> Seats = Builder ? Builder->GetSpawnTransforms() : TArray<FTransform>();
	if (Seats.IsEmpty())
	{
		UE_LOG(LogHCMatch, Error, TEXT("Meeting: this map has no seats, nobody is moved"));
		return;
	}
	FVector Center = FVector::ZeroVector;
	for (const FTransform& Seat : Seats)
	{
		Center += Seat.GetLocation();
	}
	Center /= Seats.Num();

	// Fixed seats in player order (identity), so everyone always sits in the same place
	TArray<AHCPlayerState*> Living = GetEligible();
	Living.Sort([](const AHCPlayerState& A, const AHCPlayerState& B) { return A.GetIdentity() < B.GetIdentity(); });
	TSet<int32> Used;
	int32 Seated = 0;
	for (AHCPlayerState* PS : Living)
	{
		AHCPlayerController* PC = Cast<AHCPlayerController>(PS->GetOwningController());
		AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
		if (!Character)
		{
			continue;
		}
		int32 SeatIndex = FMath::Max(0, PS->GetIdentity()) % Seats.Num();
		for (int32 Try = 0; Try < Seats.Num() && Used.Contains(SeatIndex); ++Try)
		{
			SeatIndex = (SeatIndex + 1) % Seats.Num();
		}
		Used.Add(SeatIndex);
		const FVector Location = Seats[SeatIndex].GetLocation();
		const FRotator Facing(0.f, (Center - Location).Rotation().Yaw, 0.f);

		Character->ServerSetKillLocked(false);
		Character->GetCharacterMovement()->StopMovementImmediately();
		// Other players do not block (pawns overlap each other); only walls and props can be in the way
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HCMeetingSeat), false, Character);
		const bool bBlocked = GetWorld()->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(38.f, 86.f), Params);
		if (bBlocked || !Character->TeleportTo(Location, Facing, false, true))
		{
			UE_LOG(LogHCMatch, Warning, TEXT("HC_FAILSAFE: seat %d of %s is blocked, using a safe point"), SeatIndex, *PS->GetPlayerName());
			Character->TeleportToSafePoint(Location);
		}
		else
		{
			++Seated;
		}
		PC->ClientSetRotation(Facing, true);
	}
	UE_LOG(LogHCMatch, Log, TEXT("Meeting: %d of %d living players seated"), Seated, Living.Num());
}

void AHCMeetingManager::BeginVoting()
{
	Votes.Reset();
	for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
	{
		PS->ServerSetHasVoted(false);
	}
	SetStage(EHCMeetingStage::Voting, GameMode->Rules ? GameMode->Rules->VotingTime : 45.f);
}

bool AHCMeetingManager::CastVote(AHCPlayerState* Voter, int32 TargetPlayerId, FText& OutError)
{
	if (Stage != EHCMeetingStage::Voting || !Voter)
	{
		OutError = NSLOCTEXT("HC", "VoteNotNow", "Gerade läuft keine Abstimmung.");
		return false;
	}
	if (!Voter->IsAlive())
	{
		OutError = NSLOCTEXT("HC", "VoteGhost", "Geister stimmen nicht ab.");
		return false;
	}
	if (Votes.Contains(Voter->GetPlayerId()))
	{
		OutError = NSLOCTEXT("HC", "VoteFinal", "Deine Stimme ist bereits abgegeben.");
		return false;
	}
	if (TargetPlayerId != FHCVoteTally::SkipId)
	{
		const AHCPlayerState* Target = FindPlayer(TargetPlayerId);
		if (!Target || !Target->IsAlive())
		{
			OutError = NSLOCTEXT("HC", "VoteBadTarget", "Diese Person kann nicht gewählt werden.");
			return false;
		}
	}
	Votes.Add(Voter->GetPlayerId(), TargetPlayerId);
	Voter->ServerSetHasVoted(true);
	UE_LOG(LogHCMatch, Log, TEXT("Vote: %s voted (%d of %d)"), *Voter->GetPlayerName(), Votes.Num(), GetEligible().Num());
	if (HaveAllVoted())
	{
		BeginResults(); // everyone voted: no need to wait for the timer (docs/02 §8.3)
	}
	return true;
}

void AHCMeetingManager::BeginResults()
{
	const UHCMatchRules* Rules = GameMode->Rules;
	TArray<int32> Ids;
	for (const AHCPlayerState* PS : GetEligible())
	{
		Ids.Add(PS->GetPlayerId());
	}
	const FHCVoteOutcome Outcome = FHCVoteTally::Count(Ids, Ids, Votes);
	EjectedId = Outcome.EjectedId;

	// The host's lobby choice (the game state carries it from the lobby into the round)
	const AHCGameState* VoteGS = GetHCGameState();
	const bool bAnonymous = VoteGS ? VoteGS->AreVotesAnonymous() : Rules && Rules->bAnonymousVotes;
	TArray<FHCVoteResultEntry> Entries;
	FString Summary;
	auto AddEntry = [&](int32 TargetId)
	{
		FHCVoteResultEntry Entry;
		Entry.TargetPlayerId = TargetId;
		Entry.Count = Outcome.Counts.FindRef(TargetId);
		if (!bAnonymous)
		{
			Entry.VoterIds = Outcome.VotersByTarget.FindRef(TargetId);
		}
		Entries.Add(Entry);
		const AHCPlayerState* Target = FindPlayer(TargetId);
		Summary += FString::Printf(TEXT("%s=%d "), Target ? *Target->GetPlayerName() : TEXT("skip"), Entry.Count);
	};
	for (const int32 Id : Ids)
	{
		AddEntry(Id);
	}
	AddEntry(FHCVoteTally::SkipId);

	const AHCPlayerState* Ejected = FindPlayer(EjectedId);
	const FText Text = Ejected
		? FText::Format(NSLOCTEXT("HC", "VoteEjects", "{0} wird ausgeworfen."), FText::FromString(Ejected->GetPlayerName()))
		: NSLOCTEXT("HC", "VoteNobody", "Niemand wurde ausgeworfen.");
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetVoteResults(Entries, Ejected ? EjectedId : INDEX_NONE, Text);
	}
	UE_LOG(LogHCMatch, Log, TEXT("Vote result%s: %s-> %s"), bAnonymous ? TEXT(" (anonymous, voters not sent)") : TEXT(""), *Summary,
		Ejected ? *Ejected->GetPlayerName() : TEXT("nobody"));
	SetStage(EHCMeetingStage::Results, Rules ? Rules->ResultsTime : 5.f);
}

void AHCMeetingManager::AfterResults()
{
	AHCPlayerState* Ejected = FindPlayer(EjectedId);
	if (Ejected && Ejected->IsAlive())
	{
		BeginEjection(Ejected);
	}
	else
	{
		ResumePlaying();
	}
}

void AHCMeetingManager::BeginEjection(AHCPlayerState* Ejected)
{
	const UHCMatchRules* Rules = GameMode->Rules;
	const bool bMonster = Ejected->GetRoleInfo().Role == EHCRole::Monster;
	const FText Name = FText::FromString(Ejected->GetPlayerName());
	FText Text = FText::Format(NSLOCTEXT("HC", "EjectedPlain", "{0} wurde ausgeworfen."), Name);
	if (!Rules || Rules->bConfirmEjectRole)
	{
		Text = bMonster ? FText::Format(NSLOCTEXT("HC", "EjectedMonster", "{0} war der Wandler."), Name)
			: FText::Format(NSLOCTEXT("HC", "EjectedCrew", "{0} war nicht der Wandler."), Name);
	}
	Stage = EHCMeetingStage::None;
	GetWorldTimerManager().ClearTimer(StageTimer);
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetMeetingStage(EHCMeetingStage::None);
		GS->ServerSetMeetingText(Text);
	}
	UE_LOG(LogHCMatch, Log, TEXT("Ejection: %s (%s)"), *Ejected->GetPlayerName(), bMonster ? TEXT("Monster") : TEXT("Crew"));

	// The ejected player becomes a ghost in the meeting room, without remains (docs/02 §8.7)
	Ejected->ServerSetAlive(false);
	GameMode->bMonsterEjected |= bMonster;
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(Ejected->GetOwningController()))
	{
		AHCCharacter* Character = Cast<AHCCharacter>(PC->GetPawn());
		const FVector Location = Character ? Character->GetCamera()->GetComponentLocation() : FVector(0.f, 0.f, 200.f);
		GameMode->MakeGhost(PC, Location, FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
		if (Character)
		{
			Character->Destroy();
		}
	}
	// The game mode's phase timer calls OnEjectionFinished
	GameMode->SetPhase(EHCMatchPhase::Ejection, Rules ? Rules->EjectionTime : 8.f);
}

void AHCMeetingManager::OnEjectionFinished()
{
	ResumePlaying();
}

void AHCMeetingManager::ResumePlaying()
{
	Abort();
	// Only now the win check: ejected monster, or parity after a wrong ejection (docs/02 §8.7)
	GameMode->EvaluateWinConditions();
	const EHCMatchPhase Phase = GameMode->GetPhase();
	if (Phase != EHCMatchPhase::Meeting && Phase != EHCMatchPhase::Ejection)
	{
		return; // match over
	}
	const UHCMatchRules* Rules = GameMode->Rules;
	GameMode->SetPhase(EHCMatchPhase::Playing, 0.f);
	GameMode->GetSabotage()->OnPlayResumed();
	const double Now = GameMode->GetServerTime();
	// Kill cooldown back to full, the emergency button is locked for a moment (docs/02 §8.7)
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		It->ServerSetKillLocked(false);
		It->GetKillComponent()->ServerSetReadyTime(Now + (Rules ? Rules->KillCooldown : 30.f));
		if (!Rules || Rules->bResetShapeshiftCooldownAfterMeeting)
		{
			It->GetShapeshift()->ServerSetReadyTime(Now + (Rules ? Rules->ShapeshiftCooldown : 30.f));
		}
	}
	EmergencyReadyTime = Now + (Rules ? Rules->EmergencyButtonCooldown : 20.f);
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetEmergencyReadyTime(EmergencyReadyTime);
	}
	for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
	{
		PS->ServerSetHasVoted(false);
	}
	GameMode->PublishTaskProgress();
	UE_LOG(LogHCMatch, Log, TEXT("Meeting: over, the round goes on"));
}

// ---------------------------------------------------------------------------
// Chat

bool AHCMeetingManager::PostChat(const AHCPlayerState* Sender, const FString& RawText)
{
	if (!GameMode || !Sender || !HCChat::IsOpen(GameMode->GetPhase(), Stage))
	{
		return false;
	}
	const FString Text = HCChat::Sanitize(RawText);
	if (Text.IsEmpty())
	{
		return false;
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	double& Last = LastChatTime.FindOrAdd(Sender->GetPlayerId(), -1000.0);
	if (Now - Last < HCChat::ServerMinInterval)
	{
		UE_LOG(LogHCMatch, Verbose, TEXT("Chat: line of %s dropped (too fast)"), *Sender->GetPlayerName());
		return false;
	}
	Last = Now;

	FHCChatLine Line;
	Line.SenderId = Sender->GetPlayerId();
	Line.SenderName = Sender->GetPlayerName();
	Line.SenderIdentity = Sender->GetIdentity();
	Line.bFromGhost = !Sender->IsAlive();
	Line.Text = Text;
#if !UE_BUILD_SHIPPING
	UE_LOG(LogHCMatch, Log, TEXT("Chat: %s%s: %s"), *Line.SenderName, Line.bFromGhost ? TEXT(" (ghost)") : TEXT(""), *Line.Text);
#endif
	DeliverChatLine(MoveTemp(Line));
	return true;
}

void AHCMeetingManager::DeliverChatLine(FHCChatLine Line)
{
	if (!GameMode)
	{
		return;
	}
	const AHCGameState* GS = GetHCGameState();
	Line.MatchSerial = GS ? GS->GetMatchSerial() : 0;
	Line.MeetingNumber = MeetingNumber;
	for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
	{
		AHCPlayerController* PC = Cast<AHCPlayerController>(PS->GetOwningController());
		if (PC && HCChat::CanRead(!Line.bFromGhost, PS->IsAlive()))
		{
			PC->ClientReceiveChat(Line);
		}
	}
}

// ---------------------------------------------------------------------------
// Disconnects (docs/02 §8.8)

void AHCMeetingManager::NotifyPlayerLeft(int32 PlayerId)
{
	if (Stage == EHCMeetingStage::None)
	{
		return;
	}
	Votes.Remove(PlayerId);
	// Votes for the leaver are handed back: those players may choose again
	TArray<int32> Returned;
	for (const TPair<int32, int32>& Pair : Votes)
	{
		if (Pair.Value == PlayerId)
		{
			Returned.Add(Pair.Key);
		}
	}
	for (const int32 VoterId : Returned)
	{
		Votes.Remove(VoterId);
		if (Stage != EHCMeetingStage::Voting)
		{
			continue;
		}
		if (AHCPlayerState* Voter = FindPlayer(VoterId))
		{
			Voter->ServerSetHasVoted(false);
			if (AHCPlayerController* PC = Cast<AHCPlayerController>(Voter->GetOwningController()))
			{
				PC->ClientShowMessage(NSLOCTEXT("HC", "VoteAgain", "Deine Wahl hat das Spiel verlassen. Bitte neu wählen."));
			}
		}
	}
	if (Stage == EHCMeetingStage::Voting && HaveAllVoted())
	{
		BeginResults();
	}
}
