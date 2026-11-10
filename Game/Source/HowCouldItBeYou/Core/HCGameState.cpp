#include "Core/HCGameState.h"

#include "Core/HCGameInstance.h"
#include "Core/HCLog.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Online/HCSessionSubsystem.h"

float AHCGameState::GetPhaseRemainingSeconds() const
{
	if (PhaseEndServerTime <= 0.0)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Max(0.0, PhaseEndServerTime - GetServerWorldTimeSeconds()));
}

void AHCGameState::ServerSetPhase(EHCMatchPhase NewPhase, float DurationSeconds)
{
	check(HasAuthority());
	const EHCMatchPhase Old = MatchPhase;
	MatchPhase = NewPhase;
	PhaseStartServerTime = GetServerWorldTimeSeconds();
	PhaseEndServerTime = DurationSeconds > 0.f ? PhaseStartServerTime + DurationSeconds : 0.0;
	UE_LOG(LogHCMatch, Log, TEXT("Phase %s -> %s (%.0f s)"), HCIdentity::PhaseName(Old), HCIdentity::PhaseName(NewPhase), DurationSeconds);
	OnRep_MatchPhase(); // listen server: RepNotify does not run on the server in C++
}

void AHCGameState::ServerSetPhaseEnd(float SecondsFromNow)
{
	check(HasAuthority());
	PhaseEndServerTime = SecondsFromNow > 0.f ? GetServerWorldTimeSeconds() + SecondsFromNow : 0.0;
}

void AHCGameState::ServerSetResult(EHCWinner InWinner, const FText& InReason, const TArray<FHCRevealedRole>& InRoles)
{
	check(HasAuthority());
	Winner = InWinner;
	WinReason = InReason;
	RevealedRoles = InRoles;
}

void AHCGameState::ServerSetLobbyInfo(const FString& InJoinCode, const FString& InHostAddress, const FString& InModeName)
{
	check(HasAuthority());
	JoinCode = InJoinCode;
	HostAddress = InHostAddress;
	ModeName = InModeName;
}

void AHCGameState::ServerSetTaskProgress(int32 InCompleted, int32 InTotal, bool bInVisible)
{
	check(HasAuthority());
	CompletedTasks = InCompleted;
	TotalTasks = InTotal;
	bTaskBarVisible = bInVisible;
}

void AHCGameState::OnRep_MatchPhase()
{
	// HUD polls the phase every frame; nothing else to do yet.
}

void AHCGameState::MulticastAnnouncement_Implementation(const FText& Text, float Seconds)
{
	Announcement = Text;
	AnnouncementUntil = FPlatformTime::Seconds() + Seconds;
}

FText AHCGameState::GetAnnouncement() const
{
	return FPlatformTime::Seconds() < AnnouncementUntil ? Announcement : FText::GetEmpty();
}

void AHCGameState::MulticastHostEnded_Implementation()
{
	if (HasAuthority())
	{
		return; // the host leaves on its own
	}
	UGameInstance* GameInstance = GetGameInstance();
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GameInstance))
	{
		GI->SetMenuMessage(NSLOCTEXT("HC", "HostEnded", "Der Host hat das Spiel verlassen."));
	}
	if (UHCSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UHCSessionSubsystem>() : nullptr)
	{
		// Do not change the map inside the RPC; the session subsystem opens the menu itself.
		Sessions->LeaveToMainMenu();
	}
}

void AHCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCGameState, MatchPhase);
	DOREPLIFETIME(AHCGameState, PhaseStartServerTime);
	DOREPLIFETIME(AHCGameState, PhaseEndServerTime);
	DOREPLIFETIME(AHCGameState, MatchSerial);
	DOREPLIFETIME(AHCGameState, CompletedTasks);
	DOREPLIFETIME(AHCGameState, TotalTasks);
	DOREPLIFETIME(AHCGameState, bTaskBarVisible);
	DOREPLIFETIME(AHCGameState, Winner);
	DOREPLIFETIME(AHCGameState, WinReason);
	DOREPLIFETIME(AHCGameState, RevealedRoles);
	DOREPLIFETIME(AHCGameState, JoinCode);
	DOREPLIFETIME(AHCGameState, HostAddress);
	DOREPLIFETIME(AHCGameState, ModeName);
}
