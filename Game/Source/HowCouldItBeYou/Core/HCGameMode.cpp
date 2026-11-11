#include "Core/HCGameMode.h"

#include "Abilities/HCKillComponent.h"
#include "Abilities/HCRemains.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCGhostPawn.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCMatchRules.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Meeting/HCEmergencyButton.h"
#include "Meeting/HCMeetingManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Online/HCSessionSubsystem.h"
#include "Roles/HCRoleAssigner.h"
#include "Roles/HCWinConditionEvaluator.h"
#include "Sabotage/HCFuseBox.h"
#include "Sabotage/HCSabotageManager.h"
#include "Tasks/HCTaskAssigner.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "TimerManager.h"
#include "UI/HCHUD.h"
#include "World/HCStationBuilder.h"

AHCGameMode::AHCGameMode()
{
	DefaultPawnClass = AHCCharacter::StaticClass();
	PlayerControllerClass = AHCPlayerController::StaticClass();
	PlayerStateClass = AHCPlayerState::StaticClass();
	GameStateClass = AHCGameState::StaticClass();
	HUDClass = AHCHUD::StaticClass();
	bStartPlayersAsSpectators = false;
	// Lobby <-> match without dropping connections, via Map_Transition (docs/02 §5.3)
	bUseSeamlessTravel = true;
}

AHCGameState* AHCGameMode::GetHCGameState() const
{
	return GetGameState<AHCGameState>();
}

UHCGameInstance* AHCGameMode::GetHCGameInstance() const
{
	return Cast<UHCGameInstance>(GetGameInstance());
}

TArray<AHCPlayerState*> AHCGameMode::GetHCPlayerStates() const
{
	TArray<AHCPlayerState*> Result;
	if (const AGameStateBase* GS = GameState)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
			{
				if (!HCPS->IsInactive())
				{
					Result.Add(HCPS);
				}
			}
		}
	}
	return Result;
}

EHCMatchPhase AHCGameMode::GetPhase() const
{
	const AHCGameState* GS = GetHCGameState();
	return GS ? GS->GetMatchPhase() : EHCMatchPhase::Lobby;
}

bool AHCGameMode::IsLobbyMap() const
{
	const FString Lobby = FPackageName::GetShortName(UHCGameSettings::Get()->LobbyMap);
	return GetWorld() && GetWorld()->GetMapName().EndsWith(Lobby);
}

void AHCGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	Rules = UHCMatchRules::Load(this, UHCGameSettings::Get()->DefaultModeId);
}

void AHCGameMode::StartPlay()
{
	Super::StartPlay();

	UHCGameInstance* GI = GetHCGameInstance();
	if (IsLobbyMap())
	{
		SetPhase(EHCMatchPhase::Lobby, 0.f);
		PublishLobbyInfo();
		if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
		{
			Sessions->SetMatchRunning(false);
		}
		return;
	}

	ExpectedTravellers = GI ? GI->ExpectedTravellers : 0;
	if (GI)
	{
		GI->ExpectedTravellers = 0;
	}
	bCameFromLobby = ExpectedTravellers > 0;
	if (!bCameFromLobby)
	{
		// Quick test: match map opened directly. Free roam until hc.StartMatch.
		SetPhase(EHCMatchPhase::Lobby, 0.f);
		PublishLobbyInfo();
		UE_LOG(LogHCMatch, Log, TEXT("Match map opened directly - free roam. Start a round with hc.StartMatch."));
		return;
	}
	bPlayIntro = UHCGameSettings::Get()->bIntroEnabled && GI && !GI->bIntroSeenThisSession;
	SetPhase(EHCMatchPhase::Loading, UHCGameSettings::Get()->AllPlayersLoadedTimeoutSeconds);
	LoadingStartedAt = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(LoadingTimer, this, &AHCGameMode::CheckTravellersLoaded, 0.5f, true, 0.5f);
}

void AHCGameMode::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	UE_LOG(LogHCNet, Log, TEXT("Seamless travel finished on %s with %d player(s)"), *GetWorld()->GetMapName(), GetNumPlayers());
}

void AHCGameMode::PublishLobbyInfo()
{
	AHCGameState* GS = GetHCGameState();
	UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>();
	if (GS)
	{
		GS->ServerSetLobbyInfo(Sessions ? Sessions->GetJoinCode() : FString(), UHCSessionSubsystem::GetLocalAddress(), Rules ? Rules->DisplayName : FString());
	}
#if !UE_BUILD_SHIPPING
	// Smoke test: hand the join code to a client process
	FString CodeFile;
	if (Sessions && !Sessions->GetJoinCode().IsEmpty() && FParse::Value(FCommandLine::Get(), TEXT("HCWriteCodeFile="), CodeFile))
	{
		FFileHelper::SaveStringToFile(Sessions->GetJoinCode(), *CodeFile);
	}
#endif
}

void AHCGameMode::SetPhase(EHCMatchPhase NewPhase, float DurationSeconds)
{
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetPhase(NewPhase, DurationSeconds);
	}
	if (DurationSeconds > 0.f && NewPhase != EHCMatchPhase::Loading)
	{
		GetWorldTimerManager().SetTimer(PhaseTimer, this, &AHCGameMode::OnPhaseTimer, DurationSeconds, false);
	}
}

void AHCGameMode::OnPhaseTimer()
{
	switch (GetPhase())
	{
	case EHCMatchPhase::Intro:
		BeginRoleReveal();
		break;
	case EHCMatchPhase::RoleReveal:
		BeginPlaying();
		break;
	case EHCMatchPhase::Ejection:
		GetMeeting()->OnEjectionFinished();
		break;
	case EHCMatchPhase::GameOver:
		ReturnToLobby();
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Joining and leaving

void AHCGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	// Version check (docs/03 §7.4). PIE clients send no version, they are always the same build.
	const FString ClientVersion = UGameplayStatics::ParseOption(Options, TEXT("BuildVer"));
	if (!ClientVersion.IsEmpty() && ClientVersion != UHCSessionSubsystem::GetBuildVersion())
	{
		ErrorMessage = FText::Format(NSLOCTEXT("HC", "JoinWrongVersionFmt", "Falsche Version: Host {0}, du {1}"), FText::FromString(UHCSessionSubsystem::GetBuildVersion()), FText::FromString(ClientVersion)).ToString();
	}
	else if (GetPhase() != EHCMatchPhase::Lobby)
	{
		ErrorMessage = NSLOCTEXT("HC", "JoinRoundRunning", "Runde läuft gerade, versuch es nach der Runde.").ToString();
	}
	else if (GetNumPlayers() >= UHCGameSettings::Get()->MaxPlayers)
	{
		ErrorMessage = NSLOCTEXT("HC", "JoinLobbyFull", "Lobby ist voll.").ToString();
	}
	if (!ErrorMessage.IsEmpty())
	{
		UE_LOG(LogHCNet, Log, TEXT("Rejected join from %s: %s"), *Address, *ErrorMessage);
	}
}

FString AHCGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	// The online subsystem replaces "Name=" with its own nickname, so the chosen name travels as HCName
	const FString ChosenName = UGameplayStatics::ParseOption(Options, TEXT("HCName"));
	if (!ChosenName.IsEmpty())
	{
		ChangeName(NewPlayerController, UHCSessionSubsystem::SanitizePlayerName(ChosenName), false);
	}
	return Error;
}

void AHCGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (AHCPlayerState* PS = NewPlayer->GetPlayerState<AHCPlayerState>())
	{
		AssignIdentity(PS);
		UE_LOG(LogHCNet, Log, TEXT("%s joined (identity %d)"), *PS->GetPlayerName(), PS->GetIdentity());
		if (AHCGameState* GS = GetHCGameState(); GS && !NewPlayer->IsLocalController())
		{
			GS->MulticastAnnouncement(FText::Format(NSLOCTEXT("HC", "Joined", "{0} ist beigetreten."), FText::FromString(PS->GetPlayerName())), 4.f);
		}
	}
}

int32 AHCGameMode::AssignIdentity(AHCPlayerState* PlayerState)
{
	if (PlayerState->GetIdentity() != INDEX_NONE)
	{
		return PlayerState->GetIdentity();
	}
	TSet<int32> Used;
	for (AHCPlayerState* Other : GetHCPlayerStates())
	{
		if (Other != PlayerState && Other->GetIdentity() != INDEX_NONE)
		{
			Used.Add(Other->GetIdentity());
		}
	}
	int32 Free = 0;
	while (Used.Contains(Free) && Free < HCIdentity::Count - 1)
	{
		++Free;
	}
	PlayerState->ServerSetIdentity(Free);
	return Free;
}

void AHCGameMode::Logout(AController* Exiting)
{
	AHCPlayerState* PS = Exiting ? Exiting->GetPlayerState<AHCPlayerState>() : nullptr;
	const EHCMatchPhase Phase = GetPhase();
	if (PS)
	{
		UE_LOG(LogHCNet, Log, TEXT("%s left during %s"), *PS->GetPlayerName(), HCIdentity::PhaseName(Phase));
	}
	CancelKillsOf(Exiting);
	if (PS && !Exiting->IsLocalController())
	{
		const bool bInRound = Phase == EHCMatchPhase::RoleReveal || Phase == EHCMatchPhase::Playing
			|| Phase == EHCMatchPhase::Meeting || Phase == EHCMatchPhase::Ejection;
		if (AHCGameState* GS = GetHCGameState())
		{
			// Same message for living players and ghosts, reveals nothing (docs/03 §7.5)
			GS->MulticastAnnouncement(FText::Format(NSLOCTEXT("HC", "LostConnection", "{0} hat die Verbindung verloren."), FText::FromString(PS->GetPlayerName())), 5.f);
		}
		if (bInRound)
		{
			const bool bWasMonster = PS->GetRoleInfo().Role == EHCRole::Monster && PS->IsAlive();
			// Open real tasks of the leaver leave the total at once, otherwise the crew could never finish (docs/02 §5.5)
			if (UHCTaskComponent* Tasks = PS->GetTaskComponent(); Tasks && !Tasks->IsFakeList())
			{
				TrueTotalTasks = FMath::Max(TrueCompletedTasks, TrueTotalTasks - Tasks->CountOpen());
				Tasks->ServerClear();
				PublishTaskProgress();
			}
			PS->ServerSetAlive(false); // counts as dead, no remains, no ghost
			const int32 LeftId = PS->GetPlayerId();
			NotifyShapeshifters(LeftId, true);
			Super::Logout(Exiting);
			EvaluateWinConditions(bWasMonster);
			if (Meeting)
			{
				Meeting->NotifyPlayerLeft(LeftId);
			}
			return;
		}
	}
	Super::Logout(Exiting);
}

// ---------------------------------------------------------------------------
// Spawning

void AHCGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}
	AHCPlayerState* PS = NewPlayer->GetPlayerState<AHCPlayerState>();
	const int32 Identity = PS ? AssignIdentity(PS) : 0;

	AHCStationBuilder* Builder = nullptr;
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		Builder = *It;
		break;
	}
	const TArray<FTransform> Spawns = Builder ? Builder->GetSpawnTransforms() : TArray<FTransform>();
	if (Spawns.Num() > 0)
	{
		// Prefer the identity's own spawn point, then any free one (capsule overlap check)
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(40.f, 90.f);
		FTransform Chosen = Spawns[Identity % Spawns.Num()];
		for (int32 i = 0; i < Spawns.Num(); ++i)
		{
			const FTransform& Candidate = Spawns[(Identity + i) % Spawns.Num()];
			if (!GetWorld()->OverlapAnyTestByChannel(Candidate.GetLocation(), FQuat::Identity, ECC_Pawn, Capsule))
			{
				Chosen = Candidate;
				break;
			}
		}
		RestartPlayerAtTransform(NewPlayer, Chosen);
	}
	else
	{
		Super::RestartPlayer(NewPlayer);
	}

	if (AHCCharacter* Character = Cast<AHCCharacter>(NewPlayer->GetPawn()))
	{
		Character->ServerSetAppearance(Identity, PS ? PS->GetPlayerName() : FString());
		UE_LOG(LogHCMatch, Log, TEXT("Spawned %s (identity %d) at %s"), PS ? *PS->GetPlayerName() : *GetNameSafe(NewPlayer), Identity, *Character->GetActorLocation().ToCompactString());
	}
}

// ---------------------------------------------------------------------------
// Round flow

bool AHCGameMode::RequestStartMatch(APlayerController* Requester, FText& OutError)
{
	if (Requester && !Requester->IsLocalController())
	{
		OutError = NSLOCTEXT("HC", "OnlyHost", "Nur der Host kann die Runde starten.");
		return false;
	}
	if (GetPhase() != EHCMatchPhase::Lobby)
	{
		OutError = NSLOCTEXT("HC", "NotInLobby", "Es läuft bereits eine Runde.");
		return false;
	}
	const int32 Players = GetNumPlayers();
	if (Players < UHCGameSettings::Get()->MinPlayersToStart)
	{
		OutError = FText::Format(NSLOCTEXT("HC", "TooFew", "Mindestens {0} Spieler nötig."), UHCGameSettings::Get()->MinPlayersToStart);
		return false;
	}

	UHCGameInstance* GI = GetHCGameInstance();
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->SetMatchRunning(true);
	}
	if (IsLobbyMap())
	{
		if (GI)
		{
			GI->ExpectedTravellers = Players;
		}
		UE_LOG(LogHCMatch, Log, TEXT("Host starts the match with %d player(s), travelling to %s"), Players, *UHCGameSettings::Get()->MatchMap);
		// Lock the lobby while travelling
		SetPhase(EHCMatchPhase::Loading, 0.f);
		GetWorld()->ServerTravel(UHCGameSettings::Get()->MatchMap, false);
		return true;
	}
	// Quick test on the match map: start right here
	bPlayIntro = UHCGameSettings::Get()->bIntroEnabled && GI && !GI->bIntroSeenThisSession;
	BeginRound();
	return true;
}

void AHCGameMode::CheckTravellersLoaded()
{
	int32 Loaded = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->HasClientLoadedCurrentWorld() && PC->GetPawn())
		{
			++Loaded;
		}
	}
	const bool bTimeout = GetWorld()->GetTimeSeconds() - LoadingStartedAt >= UHCGameSettings::Get()->AllPlayersLoadedTimeoutSeconds;
	if (Loaded >= ExpectedTravellers || bTimeout)
	{
		GetWorldTimerManager().ClearTimer(LoadingTimer);
		if (bTimeout)
		{
			UE_LOG(LogHCNet, Warning, TEXT("Loading timeout: %d of %d players arrived, starting without the rest"), Loaded, ExpectedTravellers);
		}
		BeginRound();
	}
}

void AHCGameMode::BeginRound()
{
	if (bPlayIntro)
	{
		if (UHCGameInstance* GI = GetHCGameInstance())
		{
			GI->bIntroSeenThisSession = true; // intro only for the first match of a session (docs/01 §3)
		}
		SetPhase(EHCMatchPhase::Intro, UHCGameSettings::Get()->IntroSeconds);
		return;
	}
	BeginRoleReveal();
}

void AHCGameMode::BeginRoleReveal()
{
	UHCGameInstance* GI = GetHCGameInstance();
	TArray<AHCPlayerState*> Players = GetHCPlayerStates();
	TArray<int32> Ids;
	TMap<int32, EHCRole> Forced;
	for (AHCPlayerState* PS : Players)
	{
		Ids.Add(PS->GetPlayerId());
		if (GI)
		{
			if (const EHCRole* ForcedRole = GI->ForcedRoles.Find(PS->GetPlayerName()))
			{
				Forced.Add(PS->GetPlayerId(), *ForcedRole);
			}
		}
	}
	const int32 Seed = FMath::Rand() ^ static_cast<int32>(FPlatformTime::Cycles());
	const TMap<int32, EHCRole> Roles = FHCRoleAssigner::Assign(Ids, Rules ? Rules->NumMonsters : 1, Seed, Forced);

	const uint8 Serial = GI ? ++GI->MatchSerial : 1;
	if (GI)
	{
		GI->ForcedRoles.Reset();
	}
	for (AHCPlayerState* PS : Players)
	{
		const EHCRole AssignedRole = Roles.FindRef(PS->GetPlayerId());
		PS->ServerSetRole(AssignedRole, Serial);
		PS->ServerSetAlive(true);
		// Roles only in the host log (docs/02 §18.2)
		UE_LOG(LogHCMatch, Log, TEXT("Role: %s = %s"), *PS->GetPlayerName(), AssignedRole == EHCRole::Monster ? TEXT("Monster") : TEXT("Crew"));
	}
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetResult(EHCWinner::None, FText::GetEmpty(), {});
		GS->ServerSetMatchSerial(Serial);
	}
	PlayersAtRoundStart = Players.Num();
	bMonsterEjected = false;
	GetMeeting()->ResetForRound();
	GetSabotage()->ResetForRound();
	AssignTasks(Players, Seed);
	SetPhase(EHCMatchPhase::RoleReveal, UHCGameSettings::Get()->RoleRevealSeconds);
}

void AHCGameMode::BeginPlaying()
{
	SetPhase(EHCMatchPhase::Playing, 0.f);
	GetSabotage()->OnPlayResumed();
	// First kill at the earliest 20 s after the round started (docs/01 §4.2)
	const double FirstKillAt = GetServerTime() + (Rules ? Rules->FirstKillDelay : 20.f);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AHCCharacter* Character = It->IsValid() ? Cast<AHCCharacter>((*It)->GetPawn()) : nullptr)
		{
			Character->ServerSetKillLocked(false);
			Character->GetKillComponent()->ServerSetReadyTime(FirstKillAt);
			// The shapeshift starts with its full cooldown (docs/01 §4.2)
			Character->GetShapeshift()->ServerSetReadyTime(GetServerTime() + (Rules ? Rules->ShapeshiftCooldown : 30.f));
		}
	}
}

void AHCGameMode::EvaluateWinConditions(bool bMonsterJustLeft)
{
	const EHCMatchPhase Phase = GetPhase();
	if (Phase != EHCMatchPhase::RoleReveal && Phase != EHCMatchPhase::Playing && Phase != EHCMatchPhase::Meeting && Phase != EHCMatchPhase::Ejection)
	{
		return;
	}
	FHCWinInput In;
	for (AHCPlayerState* PS : GetHCPlayerStates())
	{
		if (!PS->IsAlive())
		{
			continue;
		}
		if (PS->GetRoleInfo().Role == EHCRole::Monster)
		{
			++In.AliveMonsters;
		}
		else
		{
			++In.AliveCrew;
		}
	}
	In.bMonsterLeft = bMonsterJustLeft;
	In.bMonsterEjected = bMonsterEjected;
	if (Rules)
	{
		In.bCrewWinsByTasks = Rules->bCrewWinsByTasks;
		In.bTimerEnabled = Rules->bTimerEnabled;
		In.MinCrewAliveAtTimeout = Rules->MinCrewAliveAtTimeout;
	}
	In.CompletedTasks = TrueCompletedTasks;
	In.TotalTasks = TrueTotalTasks;
	// 1 vs 1 would end the round at once: test rounds with fewer than 3 players skip the parity rule
	In.bParityEnabled = PlayersAtRoundStart >= 3;
	FText Reason;
	const EHCWinner Winner = FHCWinConditionEvaluator::Evaluate(In, Reason);
	UE_LOG(LogHCMatch, Log, TEXT("Win check: crew %d, monsters %d, tasks %d/%d -> %s"), In.AliveCrew, In.AliveMonsters, In.CompletedTasks, In.TotalTasks,
		Winner == EHCWinner::None ? TEXT("continue") : Winner == EHCWinner::Crew ? TEXT("Crew") : TEXT("Monster"));
	if (Winner != EHCWinner::None)
	{
		EndMatch(Winner, Reason);
	}
}

void AHCGameMode::EndMatch(EHCWinner Winner, const FText& Reason)
{
	TArray<FHCRevealedRole> Revealed;
	for (AHCPlayerState* PS : GetHCPlayerStates())
	{
		FHCRevealedRole Entry;
		Entry.PlayerName = PS->GetPlayerName();
		Entry.Identity = PS->GetIdentity();
		Entry.Role = PS->GetRoleInfo().Role;
		Entry.bAlive = PS->IsAlive();
		Revealed.Add(Entry);
	}
	if (AHCGameState* GS = GetHCGameState())
	{
		GS->ServerSetResult(Winner, Reason, Revealed);
	}
	// Nobody stays locked when the round ends in the middle of a kill
	for (const FHCPendingKill& Pending : PendingKills)
	{
		if (AHCCharacter* Locked = Pending.VictimCharacter.Get())
		{
			Locked->ServerSetKillLocked(false);
		}
		if (AHCCharacter* Locked = Pending.Attacker.Get())
		{
			Locked->ServerSetKillLocked(false);
		}
	}
	PendingKills.Reset();
	if (Meeting)
	{
		Meeting->Abort();
	}
	PublishTaskProgress(true);
	UE_LOG(LogHCMatch, Log, TEXT("Match over: %s wins (%s)"), Winner == EHCWinner::Crew ? TEXT("Crew") : TEXT("Monster"), *Reason.ToString());
	SetPhase(EHCMatchPhase::GameOver, UHCGameSettings::Get()->GameOverScreenSeconds);
}

void AHCGameMode::RequestReturnToLobby(APlayerController* Requester)
{
	if (Requester && !Requester->IsLocalController())
	{
		return; // only the host
	}
	if (GetPhase() == EHCMatchPhase::GameOver)
	{
		ReturnToLobby();
	}
}

void AHCGameMode::ReturnToLobby()
{
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	if (bCameFromLobby)
	{
		UE_LOG(LogHCMatch, Log, TEXT("Back to the lobby"));
		GetWorld()->ServerTravel(UHCGameSettings::Get()->LobbyMap, false);
		return;
	}
	// Quick test on the match map: back to free roam, everyone respawns
	ResetPlayersForLobby();
	SetPhase(EHCMatchPhase::Lobby, 0.f);
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->SetMatchRunning(false);
	}
}

void AHCGameMode::ResetPlayersForLobby()
{
	for (TActorIterator<AHCRemains> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (AHCPlayerState* PS = PC->GetPlayerState<AHCPlayerState>())
		{
			PS->ServerSetAlive(true);
			PS->GetTaskComponent()->ServerClear();
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->UnPossess();
			Pawn->Destroy();
		}
		RestartPlayer(PC);
	}
}

// ---------------------------------------------------------------------------
// Tasks (docs/02 §9)

void AHCGameMode::AssignTasks(const TArray<AHCPlayerState*>& Players, int32 Seed)
{
	TrueCompletedTasks = 0;
	TrueTotalTasks = 0;

	// Only tasks whose stations all exist in this map can be given out
	TMap<FName, TSet<int32>> StepsInMap;
	for (TActorIterator<AHCTaskStation> It(GetWorld()); It; ++It)
	{
		StepsInMap.FindOrAdd(It->GetTaskId()).Add(It->GetStepIndex());
	}
	FHCTaskAssignInput Input;
	for (const FHCTaskDefinition& Def : UHCGameSettings::Get()->TaskDefinitions)
	{
		const TSet<int32>* Steps = StepsInMap.Find(Def.TaskId);
		bool bAllStepsPlaced = Steps != nullptr && Def.Steps.Num() > 0;
		for (int32 Step = 0; bAllStepsPlaced && Step < Def.Steps.Num(); ++Step)
		{
			bAllStepsPlaced = Steps->Contains(Step);
		}
		if (bAllStepsPlaced)
		{
			Input.Available.Add(Def);
		}
		else
		{
			UE_LOG(LogHCMatch, Warning, TEXT("Task %s has no station for every step in this map and is skipped"), *Def.TaskId.ToString());
		}
	}
	if (Rules)
	{
		Input.CommonTasks = Rules->CommonTasks;
		Input.LongTasks = Rules->LongTasks;
		Input.ShortTasks = Rules->ShortTasks;
	}

	TArray<int32> Ids;
	for (const AHCPlayerState* PS : Players)
	{
		Ids.Add(PS->GetPlayerId());
	}
	TArray<FString> Warnings;
	const TMap<int32, TArray<FName>> Lists = FHCTaskAssigner::Assign(Ids, Input, Seed, &Warnings);
	for (const FString& Warning : Warnings)
	{
		UE_LOG(LogHCMatch, Warning, TEXT("Task assignment: %s"), *Warning);
	}

	int32 CrewCount = 0;
	for (AHCPlayerState* PS : Players)
	{
		// The monster gets a list of the same shape; the server just never counts it (docs/02 §9.5)
		const bool bFakeList = PS->GetRoleInfo().Role == EHCRole::Monster;
		const TArray<FName> List = Lists.FindRef(PS->GetPlayerId());
		PS->GetTaskComponent()->ServerAssign(List, bFakeList);
		const int32 Assigned = PS->GetTaskComponent()->GetTasks().Num();
		if (!bFakeList)
		{
			++CrewCount;
			TrueTotalTasks += Assigned;
		}
		FString Names;
		for (const FName Id : List)
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Id.ToString();
		}
		UE_LOG(LogHCMatch, Log, TEXT("Tasks: %s gets %d%s: %s"), *PS->GetPlayerName(), Assigned, bFakeList ? TEXT(" [fake]") : TEXT(""), *Names);
	}
	if (CrewCount > 0 && TrueTotalTasks == 0)
	{
		// Without tasks the crew cannot win by tasks; an empty list never counts as "all done" (docs/02 §5.5)
		UE_LOG(LogHCMatch, Error, TEXT("No tasks could be assigned - does this map have task stations?"));
	}
	UE_LOG(LogHCMatch, Log, TEXT("Tasks: %d real tasks in total"), TrueTotalTasks);
	PublishTaskProgress();
}

void AHCGameMode::PublishTaskProgress(bool bFinal)
{
	AHCGameState* GS = GetHCGameState();
	if (!GS)
	{
		return;
	}
	const EHCTaskBarVisibility Visibility = Rules ? Rules->TaskBarVisibility : EHCTaskBarVisibility::Always;
	// "Only in meetings": the bar comes with the meeting and goes with it (docs/02 §9.3)
	const EHCMatchPhase Phase = GS->GetMatchPhase();
	const bool bMeetingNow = Visibility == EHCTaskBarVisibility::MeetingOnly && (Phase == EHCMatchPhase::Meeting || Phase == EHCMatchPhase::Ejection);
	if (bFinal || bMeetingNow || Visibility == EHCTaskBarVisibility::Always)
	{
		GS->ServerSetTaskProgress(TrueCompletedTasks, TrueTotalTasks, true);
	}
	else if (GS->GetTotalTasks() != 0 || GS->IsTaskBarVisible())
	{
		// "Only in meetings" (shown from M5 on) and "never": nothing is replicated during the round,
		// so even a cheater cannot read the progress early
		GS->ServerSetTaskProgress(0, 0, false);
	}
}

void AHCGameMode::NotifyTaskCompleted(AHCPlayerState* PlayerState, bool bFake)
{
	if (bFake)
	{
		return; // looks the same for the monster, but the bar does not move
	}
	TrueCompletedTasks = FMath::Min(TrueCompletedTasks + 1, TrueTotalTasks);
	PublishTaskProgress();
	EvaluateWinConditions();
}

// ---------------------------------------------------------------------------
// Kill, remains, ghosts (docs/02 §7)

AHCStationBuilder* AHCGameMode::FindBuilder() const
{
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AHCGameMode::IsInMeetingRoom(const FVector& Location) const
{
	// The command center is the meeting room: kill-free (docs/01 §11.1)
	const AHCStationBuilder* Builder = FindBuilder();
	return Builder && Builder->GetSpaceNameAt(Location) == Builder->GetLayout().SpawnRoom;
}

double AHCGameMode::GetServerTime() const
{
	const AHCGameState* GS = GetHCGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : 0.0;
}

EHCKillReject AHCGameMode::RequestKill(AHCCharacter* Attacker, AHCCharacter* Victim)
{
	if (!Attacker)
	{
		return EHCKillReject::AttackerDead;
	}
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const AHCPlayerState* AttackerPS = Attacker->GetPlayerState<AHCPlayerState>();
	const AHCPlayerState* VictimPS = Victim ? Victim->GetPlayerState<AHCPlayerState>() : nullptr;

	FHCKillCheck Check;
	// No new kill once a meeting has been triggered and only waits for a running kill (docs/02 §8.1)
	Check.bPhasePlaying = GetPhase() == EHCMatchPhase::Playing && !(Meeting && Meeting->IsBusy());
	Check.bAttackerIsMonster = AttackerPS && AttackerPS->GetRoleInfo().Role == EHCRole::Monster;
	Check.bAttackerAlive = AttackerPS && AttackerPS->IsAlive();
	Check.bAttackerLocked = Attacker->IsKillLocked() || Attacker->GetShapeshift()->IsChanging();
	Check.bTargetAlive = Victim && VictimPS && VictimPS->IsAlive() && Victim->GetController() != nullptr;
	Check.bTargetIsMonster = VictimPS && VictimPS->GetRoleInfo().Role == EHCRole::Monster;
	Check.bTargetLocked = Victim && Victim->IsKillLocked();
	Check.Now = GetServerTime();
	Check.KillReadyTime = Attacker->GetKillComponent()->GetReadyTime();
	Check.KillRange = Settings->KillRange;
	Check.RangeTolerance = Settings->ServerRangeTolerance;
	if (Victim)
	{
		Check.Distance = FVector::Dist(Attacker->GetActorLocation(), Victim->GetActorLocation());
		// Line of sight from the monster's eyes to the chest of the target, nothing in between
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HCKillSight), false, Attacker);
		Params.AddIgnoredActor(Victim);
		Check.bLineOfSight = !GetWorld()->LineTraceTestByChannel(Attacker->GetCamera()->GetComponentLocation(),
			Victim->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params);
		Check.bAttackerInMeetingRoom = IsInMeetingRoom(Attacker->GetActorLocation());
		Check.bTargetInMeetingRoom = IsInMeetingRoom(Victim->GetActorLocation());
	}
	const EHCKillReject Reject = FHCKillRules::Validate(Check);
	if (Reject == EHCKillReject::None)
	{
		BeginKill(Attacker, Victim);
	}
	return Reject;
}

void AHCGameMode::BeginKill(AHCCharacter* Attacker, AHCCharacter* Victim)
{
	AHCPlayerController* VictimPC = Cast<AHCPlayerController>(Victim->GetController());
	if (!VictimPC)
	{
		return;
	}
	const AHCPlayerState* VictimPS = Victim->GetPlayerState<AHCPlayerState>();
	const APlayerState* AttackerPS = Attacker ? Attacker->GetPlayerState() : nullptr;
	UE_LOG(LogHCMatch, Log, TEXT("Kill: %s attacks %s"), AttackerPS ? *AttackerPS->GetPlayerName() : TEXT("(debug)"),
		VictimPS ? *VictimPS->GetPlayerName() : TEXT("?"));

	// Lock both: no movement, no interaction, no abilities; running tasks of the victim end (docs/02 §7.2)
	Victim->ServerSetKillLocked(true);
	if (AHCPlayerState* PS = VictimPC->GetPlayerState<AHCPlayerState>())
	{
		PS->GetTaskComponent()->ServerCancelTimedStep();
	}
	FVector ScareFrom = Victim->GetActorLocation() + Victim->GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 60.f);
	if (Attacker)
	{
		Attacker->ServerSetKillLocked(true);
		ScareFrom = Attacker->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		// Only the rotation is touched, nobody is moved (docs/02 §7.7)
		if (AHCPlayerController* AttackerPC = Cast<AHCPlayerController>(Attacker->GetController()))
		{
			AttackerPC->ClientFaceLocation(Victim->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		}
		Attacker->MulticastPlayKillAttack(Victim->GetActorLocation());
	}
	// The jumpscare itself only ever goes to the victim
	VictimPC->ClientStartJumpscare(ScareFrom);

	FHCPendingKill Pending;
	Pending.Victim = VictimPC;
	Pending.VictimCharacter = Victim;
	Pending.Attacker = Attacker;
	Pending.StartTime = GetWorld()->GetTimeSeconds();
	PendingKills.Add(Pending);
	if (!GetWorldTimerManager().IsTimerActive(KillTimer))
	{
		GetWorldTimerManager().SetTimer(KillTimer, this, &AHCGameMode::TickPendingKills, 0.1f, true);
	}
}

void AHCGameMode::NotifyJumpscareFinished(AHCPlayerController* VictimPC)
{
	for (FHCPendingKill& Pending : PendingKills)
	{
		if (Pending.Victim.Get() == VictimPC)
		{
			Pending.bVictimReady = true;
		}
	}
}

void AHCGameMode::TickPendingKills()
{
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const float LockDuration = Rules ? Rules->KillLockDuration : 2.5f;
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = PendingKills.Num() - 1; Index >= 0; --Index)
	{
		const FHCPendingKill Pending = PendingKills[Index];
		const double Elapsed = Now - Pending.StartTime;
		const bool bReady = Pending.bVictimReady && Elapsed >= LockDuration;
		// Server failsafe: after about 3 s the victim ALWAYS becomes a ghost, even without an answer (docs/02 §7.6)
		const bool bFailsafe = Elapsed >= FMath::Max(LockDuration, Settings->JumpscareServerFailsafeSeconds);
		if (bReady || bFailsafe)
		{
			PendingKills.RemoveAt(Index);
			if (!bReady)
			{
				UE_LOG(LogHCMatch, Warning, TEXT("HC_FAILSAFE: victim did not report the end of the jumpscare within %.1f s"), Elapsed);
			}
			FinishKill(Pending);
		}
	}
	if (PendingKills.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(KillTimer);
	}
}

void AHCGameMode::FinishKill(const FHCPendingKill& Pending)
{
	AHCPlayerController* VictimPC = Pending.Victim.Get();
	AHCCharacter* Victim = Pending.VictimCharacter.Get();
	AHCCharacter* Attacker = Pending.Attacker.Get();

	// The monster is free again and its cooldown starts, whatever happened to the victim
	if (Attacker)
	{
		Attacker->ServerSetKillLocked(false);
		Attacker->GetKillComponent()->ServerSetReadyTime(GetServerTime() + (Rules ? Rules->KillCooldown : 30.f));
	}
	AHCPlayerState* VictimPS = VictimPC ? VictimPC->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!VictimPC || !VictimPS || GetPhase() != EHCMatchPhase::Playing)
	{
		// Victim left or the round ended meanwhile: nothing more to do
		if (Victim)
		{
			Victim->ServerSetKillLocked(false);
		}
		return;
	}

	FVector ViewLocation = Victim ? Victim->GetCamera()->GetComponentLocation() : FVector::ZeroVector;
	const FRotator ViewRotation(0.f, VictimPC->GetControlRotation().Yaw, 0.f);
	if (Victim)
	{
		// Remains on the floor below the victim; no body, no physics (docs/01 §4.2)
		FVector Floor = Victim->GetActorLocation() - FVector(0.f, 0.f, 90.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HCRemainsFloor), false, Victim);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Victim->GetActorLocation(), Victim->GetActorLocation() - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
		{
			Floor = Hit.ImpactPoint;
		}
		const FTransform RemainsTransform(FRotator(0.f, Victim->GetActorRotation().Yaw, 0.f), Floor);
		AHCRemains* Remains = GetWorld()->SpawnActorDeferred<AHCRemains>(AHCRemains::StaticClass(), RemainsTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Remains)
		{
			Remains->Configure(VictimPS->GetPlayerName(), VictimPS->GetIdentity());
			Remains->FinishSpawning(RemainsTransform);
		}
	}

	VictimPS->ServerSetAlive(false);
	NotifyShapeshifters(VictimPS->GetPlayerId(), false);
	MakeGhost(VictimPC, ViewLocation, ViewRotation);
	if (Victim)
	{
		Victim->Destroy();
	}
	UE_LOG(LogHCMatch, Log, TEXT("Kill: %s is dead and now a ghost"), *VictimPS->GetPlayerName());
	EvaluateWinConditions();
}

void AHCGameMode::MakeGhost(AHCPlayerController* PC, const FVector& Location, const FRotator& Rotation)
{
	AHCPlayerState* PS = PC->GetPlayerState<AHCPlayerState>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AHCGhostPawn* Ghost = GetWorld()->SpawnActor<AHCGhostPawn>(AHCGhostPawn::StaticClass(), Location, Rotation, Params);
	if (!Ghost)
	{
		return;
	}
	Ghost->SetIdentity(PS ? PS->GetIdentity() : 0);
	PC->UnPossess();
	PC->Possess(Ghost);
	PC->SetControlRotation(Rotation);

	// The new ghost learns who the Wandler is - only now, and only this player (docs/02 §2.5)
	TArray<int32> MonsterIds;
	for (const AHCPlayerState* Other : GetHCPlayerStates())
	{
		if (Other->GetRoleInfo().Role == EHCRole::Monster)
		{
			MonsterIds.Add(Other->GetPlayerId());
		}
	}
	PC->ClientRevealMonstersToGhost(MonsterIds);
}

void AHCGameMode::DebugKill(APlayerController* VictimPC)
{
	AHCCharacter* Victim = VictimPC ? Cast<AHCCharacter>(VictimPC->GetPawn()) : nullptr;
	if (Victim && GetPhase() == EHCMatchPhase::Playing && !Victim->IsKillLocked())
	{
		BeginKill(nullptr, Victim);
	}
}

void AHCGameMode::CancelKillsOf(const AController* Leaver)
{
	for (int32 Index = PendingKills.Num() - 1; Index >= 0; --Index)
	{
		FHCPendingKill& Pending = PendingKills[Index];
		AHCCharacter* Attacker = Pending.Attacker.Get();
		if (Pending.Victim.Get() == Leaver)
		{
			// The victim left during the jumpscare: counts as disconnected, no remains (docs/02 §7.6)
			if (Attacker)
			{
				Attacker->ServerSetKillLocked(false);
				Attacker->GetKillComponent()->ServerSetReadyTime(GetServerTime() + (Rules ? Rules->KillCooldown : 30.f));
			}
			PendingKills.RemoveAt(Index);
		}
		else if (Attacker && Attacker->GetController() == Leaver)
		{
			// The monster left: nobody stays locked, the round ends anyway
			if (AHCCharacter* Victim = Pending.VictimCharacter.Get())
			{
				Victim->ServerSetKillLocked(false);
			}
			PendingKills.RemoveAt(Index);
		}
	}
}

void AHCGameMode::NotifyShapeshifters(int32 PlayerId, bool bDisconnected)
{
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		It->GetShapeshift()->ServerNotifyPlayerGone(PlayerId, bDisconnected);
	}
}

void AHCGameMode::ReportRemains(AHCCharacter* Reporter, AHCRemains* Remains)
{
	FText Reason;
	if (!Reporter || !Remains || GetPhase() != EHCMatchPhase::Playing || !Remains->CanInteract(Reporter, Reason))
	{
		return;
	}
	const AHCPlayerState* PS = Reporter->GetPlayerState<AHCPlayerState>();
	// The first trigger wins; a second one in the same moment is simply dropped (docs/02 §8.1)
	if (GetMeeting()->RequestMeeting(PS, Remains->GetVictimName()))
	{
		Remains->ServerMarkReported();
		UE_LOG(LogHCMatch, Log, TEXT("Report: %s reported the remains of %s"), PS ? *PS->GetPlayerName() : TEXT("?"), *Remains->GetVictimName());
	}
}

void AHCGameMode::RequestEmergencyMeeting(AHCCharacter* Who)
{
	AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!PS || !PS->IsAlive() || !Who->CanAct() || GetPhase() != EHCMatchPhase::Playing || PS->GetEmergencyMeetingsLeft() <= 0
		|| GetServerTime() < GetMeeting()->GetEmergencyReadyTime())
	{
		return;
	}
	// The use is only spent if this trigger really starts the meeting (docs/02 §8.1)
	if (GetMeeting()->RequestMeeting(PS, FString()))
	{
		PS->ServerSetEmergencyMeetingsLeft(PS->GetEmergencyMeetingsLeft() - 1);
	}
}

AHCSabotageManager* AHCGameMode::GetSabotage()
{
	if (!Sabotage)
	{
		Sabotage = GetWorld()->SpawnActor<AHCSabotageManager>();
		Sabotage->Init(this);
	}
	return Sabotage;
}

AHCMeetingManager* AHCGameMode::GetMeeting()
{
	if (!Meeting)
	{
		Meeting = GetWorld()->SpawnActor<AHCMeetingManager>();
		Meeting->Init(this);
	}
	return Meeting;
}

// ---------------------------------------------------------------------------
// Debug (docs/02 §18.1)

void AHCGameMode::ExecuteDebugCommand(const FString& Command, const TArray<FString>& Args, APlayerController* Requester)
{
#if !UE_BUILD_SHIPPING
	const bool bFromRemoteClient = Requester && !Requester->IsLocalController();
	if (bFromRemoteClient && !GIsEditor && !UHCGameSettings::Get()->bAllowClientDebugCommands && !FParse::Param(FCommandLine::Get(), TEXT("HCAllowClientDebug")))
	{
		UE_LOG(LogHCNet, Warning, TEXT("Rejected debug command '%s' from a client (bAllowClientDebugCommands is off)"), *Command);
		return;
	}
	UE_LOG(LogHC, Display, TEXT("Debug: %s %s"), *Command, *FString::Join(Args, TEXT(" ")));

	if (Command.StartsWith(TEXT("Goto")) && GetPhase() != EHCMatchPhase::Lobby && GetPhase() != EHCMatchPhase::Playing)
	{
		return; // nobody leaves a meeting or the role screen by debug teleport
	}

	if (Command == TEXT("StartMatch"))
	{
		FText Error;
		if (!RequestStartMatch(nullptr, Error))
		{
			UE_LOG(LogHC, Warning, TEXT("hc.StartMatch: %s"), *Error.ToString());
		}
	}
	else if (Command == TEXT("SkipIntro"))
	{
		if (GetPhase() == EHCMatchPhase::Intro)
		{
			BeginRoleReveal();
		}
	}
	else if (Command == TEXT("EndMatch"))
	{
		const bool bMonster = Args.Num() > 0 && Args[0].Equals(TEXT("Monster"), ESearchCase::IgnoreCase);
		const EHCMatchPhase Phase = GetPhase();
		if (Phase == EHCMatchPhase::RoleReveal || Phase == EHCMatchPhase::Playing || Phase == EHCMatchPhase::Meeting || Phase == EHCMatchPhase::Ejection)
		{
			EndMatch(bMonster ? EHCWinner::Monster : EHCWinner::Crew, NSLOCTEXT("HC", "DebugEnd", "Runde per Debug-Befehl beendet"));
		}
	}
	else if (Command == TEXT("SetTimer"))
	{
		const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.f;
		if (GetWorldTimerManager().IsTimerActive(PhaseTimer))
		{
			GetWorldTimerManager().SetTimer(PhaseTimer, this, &AHCGameMode::OnPhaseTimer, FMath::Max(0.1f, Seconds), false);
			if (AHCGameState* GS = GetHCGameState())
			{
				GS->ServerSetPhaseEnd(Seconds);
			}
		}
	}
	else if (Command == TEXT("ForceRole"))
	{
		if (Args.Num() >= 2)
		{
			const EHCRole WantedRole = Args[1].Equals(TEXT("Monster"), ESearchCase::IgnoreCase) ? EHCRole::Monster : EHCRole::Crew;
			if (UHCGameInstance* GI = GetHCGameInstance())
			{
				// Match the name case-insensitively against connected players
				FString Name = Args[0];
				for (AHCPlayerState* PS : GetHCPlayerStates())
				{
					if (PS->GetPlayerName().Equals(Args[0], ESearchCase::IgnoreCase))
					{
						Name = PS->GetPlayerName();
					}
				}
				GI->ForcedRoles.Add(Name, WantedRole);
				UE_LOG(LogHC, Display, TEXT("Next role assignment: %s = %s"), *Name, *Args[1]);
			}
		}
	}
	else if (Command == TEXT("ListPlayers"))
	{
		ListPlayers(Requester);
	}
	else if (Command == TEXT("KillMe"))
	{
		DebugKill(Requester ? Requester : GetWorld()->GetFirstPlayerController());
	}
	else if (Command == TEXT("GotoPlayer"))
	{
		// hc.GotoPlayer <Name>: puts the caller right behind another player (test helper)
		APlayerController* Target = Requester ? Requester : GetWorld()->GetFirstPlayerController();
		AHCCharacter* Character = Target ? Cast<AHCCharacter>(Target->GetPawn()) : nullptr;
		for (TActorIterator<AHCCharacter> It(GetWorld()); Character && Args.Num() > 0 && It; ++It)
		{
			const APlayerState* OtherPS = It->GetPlayerState();
			if (*It != Character && OtherPS && OtherPS->GetPlayerName().Equals(Args[0], ESearchCase::IgnoreCase))
			{
				if (Args.Num() > 1 && Args[1].Equals(TEXT("front"), ESearchCase::IgnoreCase))
				{
					// Face to face, close enough to read the chest badge
					Character->TeleportTo(It->GetActorLocation() + It->GetActorForwardVector() * 75.f, (-It->GetActorForwardVector()).Rotation(), false, true);
					break;
				}
				Character->TeleportTo(It->GetActorLocation() - It->GetActorForwardVector() * 120.f, It->GetActorRotation(), false, true);
				break;
			}
		}
	}
	else if (Command == TEXT("GotoRemains"))
	{
		// hc.GotoRemains: puts the caller next to the nearest remains that are not reported yet (test helper)
		APlayerController* Target = Requester ? Requester : GetWorld()->GetFirstPlayerController();
		AHCCharacter* Character = Target ? Cast<AHCCharacter>(Target->GetPawn()) : nullptr;
		for (TActorIterator<AHCRemains> It(GetWorld()); Character && It; ++It)
		{
			if (!It->IsReported())
			{
				Character->TeleportTo(It->GetActorLocation() + It->GetActorForwardVector() * 110.f + FVector(0.f, 0.f, 92.f), Character->GetActorRotation(), false, true);
				break;
			}
		}
	}
	else if (Command == TEXT("Sabotage"))
	{
		// hc.Sabotage Power <Nord|Ost|SW> | Door <DoorId> | Repair - without the rules for role and cooldowns
		if (Args.Num() > 0 && Args[0].Equals(TEXT("Repair"), ESearchCase::IgnoreCase))
		{
			for (uint8 SectorIndex = 1; SectorIndex <= 3; ++SectorIndex)
			{
				GetSabotage()->RepairPower(SectorIndex, nullptr);
			}
		}
		else if (Args.Num() > 1)
		{
			const bool bDoor = Args[0].Equals(TEXT("Door"), ESearchCase::IgnoreCase);
			if (!GetSabotage()->ForceSabotage(bDoor ? EHCSabotageType::DoorLock : EHCSabotageType::PowerOutage, FName(*Args[1])))
			{
				UE_LOG(LogHC, Warning, TEXT("hc.Sabotage: unknown target %s"), *Args[1]);
			}
		}
	}
	else if (Command == TEXT("GotoFuseBox"))
	{
		// hc.GotoFuseBox <Nord|Ost|SW>: puts the caller in front of a fuse box (test helper)
		APlayerController* Target = Requester ? Requester : GetWorld()->GetFirstPlayerController();
		AHCCharacter* Character = Target ? Cast<AHCCharacter>(Target->GetPawn()) : nullptr;
		uint8 SectorIndex = 0;
		const bool bKnown = Args.Num() > 0 && AHCSabotageManager::ParseSector(Args[0], SectorIndex);
		for (TActorIterator<AHCFuseBox> It(GetWorld()); Character && bKnown && It; ++It)
		{
			if (It->GetSectorIndex() == SectorIndex)
			{
				Character->TeleportTo(It->GetStandLocation(), (It->GetActorLocation() - It->GetStandLocation()).GetSafeNormal2D().Rotation(), false, true);
				break;
			}
		}
	}
	else if (Command == TEXT("StartMeeting"))
	{
		// hc.StartMeeting: a meeting as if the caller had pressed the emergency button, without using it up
		const APlayerController* Target = Requester ? Requester : GetWorld()->GetFirstPlayerController();
		GetMeeting()->RequestMeeting(Target ? Target->GetPlayerState<AHCPlayerState>() : nullptr, FString());
	}
	else if (Command == TEXT("GotoButton"))
	{
		// hc.GotoButton: puts the caller in front of the emergency button (test helper)
		APlayerController* Target = Requester ? Requester : GetWorld()->GetFirstPlayerController();
		AHCCharacter* Character = Target ? Cast<AHCCharacter>(Target->GetPawn()) : nullptr;
		for (TActorIterator<AHCEmergencyButton> It(GetWorld()); Character && It; ++It)
		{
			Character->TeleportTo(It->GetStandLocation(), (It->GetActorLocation() - It->GetStandLocation()).GetSafeNormal2D().Rotation(), false, true);
			break;
		}
	}
	else if (Command == TEXT("CheckWin"))
	{
		EvaluateWinConditions();
	}
	else if (Command == TEXT("CompleteAllTasks"))
	{
		if (GetPhase() == EHCMatchPhase::Playing)
		{
			for (AHCPlayerState* PS : GetHCPlayerStates())
			{
				if (!PS->GetTaskComponent()->IsFakeList())
				{
					PS->GetTaskComponent()->ServerCompleteAll();
				}
			}
		}
	}
	else if (Command == TEXT("GotoStation"))
	{
		// hc.GotoStation <TaskId> [Step]: puts the caller in front of a task station (test helper)
		APlayerController* Target = Requester ? Requester : GetWorld()->GetFirstPlayerController();
		AHCCharacter* Character = Target ? Cast<AHCCharacter>(Target->GetPawn()) : nullptr;
		AHCGhostPawn* Ghost = Target ? Cast<AHCGhostPawn>(Target->GetPawn()) : nullptr;
		const int32 WantedStep = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		for (TActorIterator<AHCTaskStation> It(GetWorld()); (Character || Ghost) && Args.Num() > 0 && It; ++It)
		{
			if (It->GetTaskId().ToString().Equals(Args[0], ESearchCase::IgnoreCase) && It->GetStepIndex() == WantedStep)
			{
				const FRotator Facing = (It->GetActorLocation() - It->GetStandLocation()).GetSafeNormal2D().Rotation();
				if (Character)
				{
					Character->TeleportTo(It->GetStandLocation(), Facing, false, true);
				}
				else
				{
					Ghost->ServerTeleport(It->GetStandLocation());
				}
				break;
			}
		}
	}
#endif
}

void AHCGameMode::ListPlayers(APlayerController* Requester) const
{
	// Roles only for the host itself (docs/02 §18.1)
	const bool bShowRoles = !Requester || Requester->IsLocalController();
	UE_LOG(LogHC, Display, TEXT("Players (%d), phase %s:"), GetHCPlayerStates().Num(), HCIdentity::PhaseName(GetPhase()));
	for (AHCPlayerState* PS : GetHCPlayerStates())
	{
		UE_LOG(LogHC, Display, TEXT("  #%d %s identity %d (%s) alive %d%s"), PS->GetPlayerId(), *PS->GetPlayerName(), PS->GetIdentity(),
			*HCIdentity::ColorName(PS->GetIdentity()).ToString(), PS->IsAlive() ? 1 : 0,
			bShowRoles ? *FString::Printf(TEXT(" role %s"), PS->GetRoleInfo().Role == EHCRole::Monster ? TEXT("Monster") : TEXT("Crew")) : TEXT(""));
	}
}
