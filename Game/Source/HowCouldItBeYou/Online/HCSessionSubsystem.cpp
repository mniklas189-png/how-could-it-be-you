#include "Online/HCSessionSubsystem.h"

#include "Core/HCGameInstance.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GeneralProjectSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Online/HCJoinCode.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "SocketSubsystem.h"
#include "TimerManager.h"
#include "IPAddress.h"

namespace
{
	const FName KeyJoinCode(TEXT("JOINCODE"));
	const FName KeyBuildVer(TEXT("BUILDVER"));
	const FName KeyState(TEXT("STATE"));
	/** Local session of a player who joined by address (UHCSessionSubsystem::CreateVoiceOnlySession). */
	const FName VoiceOnlySession(TEXT("HCVoiceOnly"));
}

IOnlineSessionPtr UHCSessionSubsystem::GetSessions() const
{
	return Online::GetSessionInterface(GetWorld());
}

FString UHCSessionSubsystem::GetBuildVersion()
{
#if WITH_EDITOR
	return TEXT("dev");
#else
	return GetDefault<UGeneralProjectSettings>()->ProjectVersion;
#endif
}

FString UHCSessionSubsystem::GetLocalAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return FString();
	}
	// First choice: the address the OS resolves for this PC (the same one the session advertises)
	{
		bool bCanBindAll = false;
		const TSharedRef<FInternetAddr> HostAddr = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
		const FString Ip = HostAddr->ToString(false);
		if (HostAddr->IsValid() && !Ip.StartsWith(TEXT("127.")) && !Ip.StartsWith(TEXT("0.")) && !Ip.Contains(TEXT(":")) && !Ip.StartsWith(TEXT("169.254.")))
		{
			return Ip;
		}
	}
	// Otherwise prefer the typical home-network ranges; 172.16-31.x is often a Hyper-V/WSL/Docker adapter
	auto Priority = [](const FString& Ip) -> int32
	{
		if (Ip.StartsWith(TEXT("192.168."))) return 0;
		if (Ip.StartsWith(TEXT("10."))) return 1;
		if (Ip.StartsWith(TEXT("172."))) return 3;
		return 2;
	};
	TArray<TSharedPtr<FInternetAddr>> Addresses;
	FString Best;
	if (Sockets->GetLocalAdapterAddresses(Addresses))
	{
		for (const TSharedPtr<FInternetAddr>& Addr : Addresses)
		{
			const FString Ip = Addr->ToString(false);
			if (Ip.StartsWith(TEXT("127.")) || Ip.Contains(TEXT(":")) || Ip.StartsWith(TEXT("169.254.")))
			{
				continue;
			}
			if (Best.IsEmpty() || Priority(Ip) < Priority(Best))
			{
				Best = Ip;
			}
		}
	}
	if (!Best.IsEmpty())
	{
		return Best;
	}
	bool bCanBind = false;
	const TSharedRef<FInternetAddr> Local = Sockets->GetLocalHostAddr(*GLog, bCanBind);
	return Local->ToString(false);
}

FString UHCSessionSubsystem::SanitizePlayerName(const FString& Name)
{
	FString Clean;
	for (const TCHAR C : Name.TrimStartAndEnd())
	{
		if (FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('.'))
		{
			Clean.AppendChar(C);
		}
		else if (C == TEXT(' ') || C == TEXT('_'))
		{
			Clean.AppendChar(TEXT('_'));
		}
	}
	Clean = Clean.Left(16);
	return Clean.IsEmpty() ? FString(TEXT("Forscher")) : Clean;
}

void UHCSessionSubsystem::Report(const FText& Message, bool bError)
{
	if (bError)
	{
		UE_LOG(LogHCNet, Warning, TEXT("Session: %s"), *Message.ToString());
	}
	else
	{
		UE_LOG(LogHCNet, Log, TEXT("Session: %s"), *Message.ToString());
	}
	OnStatus.Broadcast(Message, bError);
}

// ---------------------------------------------------------------------------
// Hosting

void UHCSessionSubsystem::HostLobby(const FString& PlayerName)
{
	if (bBusy)
	{
		return;
	}
	PendingName = SanitizePlayerName(PlayerName);
	FRandomStream Random(static_cast<int32>(FDateTime::Now().GetTicks() & 0x7fffffff) ^ static_cast<int32>(FPlatformTime::Cycles()));
	PendingCode = FHCJoinCode::Make(Random);

	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		// No online subsystem: still allow LAN play by IP
		Report(NSLOCTEXT("HC", "NoOnline", "Kein Online-System, Beitritt nur per IP."), false);
		JoinCode = FString();
		OpenLobbyAsHost();
		return;
	}
	bBusy = true;
	Report(NSLOCTEXT("HC", "Hosting", "Lobby wird erstellt ..."), false);
	DestroyVoiceOnlySession();
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UHCSessionSubsystem::HandleDestroyBeforeHost));
		Sessions->DestroySession(NAME_GameSession);
		return;
	}
	CreateSessionNow();
}

void UHCSessionSubsystem::HandleDestroyBeforeHost(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}
	CreateSessionNow();
}

void UHCSessionSubsystem::CreateSessionNow()
{
	IOnlineSessionPtr Sessions = GetSessions();
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.NumPublicConnections = UHCGameSettings::Get()->MaxPlayers;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = false;
	Settings.bAllowJoinViaPresence = false;
	Settings.bUseLobbiesIfAvailable = false;
	Settings.Set(KeyJoinCode, PendingCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyBuildVer, GetBuildVersion(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyState, FString(TEXT("LOBBY")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UHCSessionSubsystem::HandleCreateSession));
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		bBusy = false;
		Report(NSLOCTEXT("HC", "CreateFailed", "Lobby konnte nicht erstellt werden."), true);
	}
}

void UHCSessionSubsystem::HandleCreateSession(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}
	bBusy = false;
	if (!bWasSuccessful)
	{
		Report(NSLOCTEXT("HC", "CreateFailed", "Lobby konnte nicht erstellt werden."), true);
		return;
	}
	JoinCode = PendingCode;
	UE_LOG(LogHCNet, Log, TEXT("Hosting lobby with join code %s, LAN address %s"), *JoinCode, *GetLocalAddress());
	OpenLobbyAsHost();
}

void UHCSessionSubsystem::OpenLobbyAsHost()
{
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		GI->bLeavingOnPurpose = false;
		GI->bIntroSeenThisSession = false;
		GI->ExpectedTravellers = 0;
	}
	const FString Options = FString::Printf(TEXT("listen?HCName=%s"), *PendingName);
	UGameplayStatics::OpenLevel(GetWorld(), FName(*UHCGameSettings::Get()->LobbyMap), true, Options);
}

// ---------------------------------------------------------------------------
// Joining

void UHCSessionSubsystem::Join(const FString& CodeOrAddress, const FString& PlayerName)
{
	if (bBusy)
	{
		return;
	}
	PendingName = SanitizePlayerName(PlayerName);
	DestroyVoiceOnlySession();
	if (FHCJoinCode::LooksLikeAddress(CodeOrAddress))
	{
		Report(FText::Format(NSLOCTEXT("HC", "ConnectingIp", "Verbinde mit {0} ..."), FText::FromString(CodeOrAddress)), false);
		CreateVoiceOnlySession();
		TravelToAddress(CodeOrAddress.TrimStartAndEnd());
		return;
	}
	FString Code;
	if (!FHCJoinCode::Normalize(CodeOrAddress, Code))
	{
		Report(NSLOCTEXT("HC", "BadCode", "Ungültiger Code. 6 Zeichen, ohne 0, O, 1, I und L. Oder eine IP-Adresse."), true);
		return;
	}
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		Report(NSLOCTEXT("HC", "NoOnlineJoin", "Kein Online-System. Bitte per IP-Adresse beitreten."), true);
		return;
	}
	PendingCode = Code;
	bBusy = true;
	Report(FText::Format(NSLOCTEXT("HC", "Searching", "Suche Lobby {0} ..."), FText::FromString(FHCJoinCode::Format(Code))), false);

	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 50;
	Search->TimeoutInSeconds = 5.f;
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UHCSessionSubsystem::HandleFindSessions));
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		bBusy = false;
		Report(NSLOCTEXT("HC", "SearchFailed", "Suche fehlgeschlagen."), true);
	}
}

void UHCSessionSubsystem::HandleFindSessions(bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}
	if (!Sessions.IsValid() || !Search.IsValid())
	{
		bBusy = false;
		return;
	}

	// The Null subsystem may not filter by attributes, so filter by join code here (docs/03 §7.2)
	for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
	{
		FString Code, Version, State;
		Result.Session.SessionSettings.Get(KeyJoinCode, Code);
		if (Code != PendingCode)
		{
			continue;
		}
		Result.Session.SessionSettings.Get(KeyBuildVer, Version);
		Result.Session.SessionSettings.Get(KeyState, State);
		if (!Version.IsEmpty() && Version != GetBuildVersion())
		{
			bBusy = false;
			Report(FText::Format(NSLOCTEXT("HC", "VersionMismatch", "Der Host hat Version {0}, du hast {1}. Starte den Launcher neu."),
				FText::FromString(Version), FText::FromString(GetBuildVersion())), true);
			return;
		}
		if (State == TEXT("MATCH"))
		{
			bBusy = false;
			Report(NSLOCTEXT("HC", "MatchRunning", "Runde läuft gerade, versuch es nach der Runde."), true);
			return;
		}
		if (Sessions->GetNamedSession(NAME_GameSession))
		{
			Sessions->DestroySession(NAME_GameSession);
		}
		JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(this, &UHCSessionSubsystem::HandleJoinSession));
		Report(NSLOCTEXT("HC", "Joining", "Lobby gefunden, trete bei ..."), false);
		Sessions->JoinSession(0, NAME_GameSession, Result);
		return;
	}
	bBusy = false;
	Report(NSLOCTEXT("HC", "NotFound", "Keine Lobby mit diesem Code im Netzwerk gefunden. Tipp: per IP-Adresse des Hosts beitreten."), true);
}

void UHCSessionSubsystem::HandleJoinSession(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}
	bBusy = false;
	FString Address;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions.IsValid() || !Sessions->GetResolvedConnectString(NAME_GameSession, Address))
	{
		Report(NSLOCTEXT("HC", "JoinFailed", "Beitritt fehlgeschlagen."), true);
		return;
	}
	JoinCode = PendingCode;
	TravelToAddress(Address);
}

void UHCSessionSubsystem::TravelToAddress(const FString& Address)
{
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		GI->bLeavingOnPurpose = false;
	}
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(GetWorld());
	if (!PC)
	{
		Report(NSLOCTEXT("HC", "NoController", "Kein lokaler Spieler."), true);
		return;
	}
	const FString Url = FString::Printf(TEXT("%s?HCName=%s?BuildVer=%s"), *Address, *PendingName, *GetBuildVersion());
	UE_LOG(LogHCNet, Log, TEXT("ClientTravel to %s"), *Url);
	PC->ClientTravel(Url, TRAVEL_Absolute);
}

// ---------------------------------------------------------------------------
// Leaving

void UHCSessionSubsystem::LeaveToMainMenu()
{
	UWorld* World = GetWorld();
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (GI)
	{
		GI->bLeavingOnPurpose = true;
	}
	const bool bIsHost = World && World->GetNetMode() == NM_ListenServer;
	if (bIsHost)
	{
		// Tell everyone first, give the message 0.5 s to arrive (docs/03 §7.6)
		if (AHCGameState* GS = World->GetGameState<AHCGameState>())
		{
			GS->MulticastHostEnded();
		}
		World->GetTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateUObject(this, &UHCSessionSubsystem::FinishHostLeave), 0.5f, false);
		return;
	}
	FinishHostLeave();
}

void UHCSessionSubsystem::FinishHostLeave()
{
	DestroySessionQuietly();
	JoinCode.Reset();
	UGameplayStatics::OpenLevel(GetWorld(), FName(*UHCGameSettings::Get()->MainMenuMap), true);
}

void UHCSessionSubsystem::SetMatchRunning(bool bRunning)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		return;
	}
	if (FOnlineSessionSettings* Settings = Sessions->GetSessionSettings(NAME_GameSession))
	{
		Settings->Set(KeyState, FString(bRunning ? TEXT("MATCH") : TEXT("LOBBY")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		Sessions->UpdateSession(NAME_GameSession, *Settings, true);
	}
}

void UHCSessionSubsystem::DestroySessionQuietly()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	DestroyVoiceOnlySession();
	bBusy = false;
}

void UHCSessionSubsystem::CreateVoiceOnlySession()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || Sessions->GetNumSessions() > 0)
	{
		return; // no online system, or a session that already keeps the voice chat running
	}
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = false; // nobody can find or join it
	Settings.NumPublicConnections = 1;
	Settings.bAllowJoinInProgress = false;
	Settings.bUsesPresence = false;
	Settings.bAllowJoinViaPresence = false;
	Settings.bUseLobbiesIfAvailable = false;
	const bool bCreated = Sessions->CreateSession(0, VoiceOnlySession, Settings);
	UE_LOG(LogHCNet, Log, TEXT("Joining by address: local session for the voice chat %s"), bCreated ? TEXT("created") : TEXT("NOT created"));
}

void UHCSessionSubsystem::DestroyVoiceOnlySession()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid() && Sessions->GetNamedSession(VoiceOnlySession))
	{
		Sessions->DestroySession(VoiceOnlySession);
		UE_LOG(LogHCNet, Log, TEXT("Local session for the voice chat removed"));
	}
}
