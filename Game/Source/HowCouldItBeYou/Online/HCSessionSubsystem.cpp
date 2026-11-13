#include "Online/HCSessionSubsystem.h"

#include "Core/HCGameInstance.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Engine/NetDriver.h"
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

void UHCSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LanTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UHCSessionSubsystem::TickLan), 0.f);

	// Online play (M9): log in by Device ID right away, so hosting and joining by code work over the internet
	EosStateHandle = IHCEos::Get().OnStateChanged.AddUObject(this, &UHCSessionSubsystem::HandleEosState);
	FString Name;
	if (const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		Name = GI->GetPlayerName();
	}
	IHCEos::Get().Start(SanitizePlayerName(Name));
}

void UHCSessionSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(LanTicker);
	LanTicker.Reset();
	StopLan();
	IHCEos::Get().OnStateChanged.Remove(EosStateHandle);
	if (bOnlineLobby)
	{
		IHCEos::Get().DestroyLobby();
		bOnlineLobby = false;
	}
	Super::Deinitialize();
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

FText UHCSessionSubsystem::GetOnlineStatus(bool& bOutError) const
{
	bOutError = false;
	switch (IHCEos::Get().GetState())
	{
	case EHCEosState::Ready:
		return NSLOCTEXT("HC", "OnlineReady", "Online bereit: Freunde treten über das Internet mit deinem Code bei.");
	case EHCEosState::LoggingIn:
		return NSLOCTEXT("HC", "OnlineConnecting", "Verbinde mit dem Online-Dienst ...");
	case EHCEosState::Failed:
		bOutError = true;
		return NSLOCTEXT("HC", "OnlineFailed", "Online-Dienst gerade nicht erreichbar: nur Heimnetz (LAN/IP). Neuer Versuch läuft.");
	case EHCEosState::NoCredentials:
		bOutError = true;
		return NSLOCTEXT("HC", "OnlineNoCredentials", "Online nicht eingerichtet: nur Heimnetz (LAN/IP).");
	default:
#if PLATFORM_LINUX
		return NSLOCTEXT("HC", "OnlineLinux", "Unter Linux nur Heimnetz (LAN/IP): Online-Beitritt gibt es bisher nur unter Windows.");
#else
		return FText::GetEmpty();
#endif
	}
}

void UHCSessionSubsystem::HandleEosState(EHCEosState State)
{
	if (bBusy)
	{
		return; // a search or hosting is running, its own messages are more important
	}
	bool bError = false;
	const FText Text = GetOnlineStatus(bError);
	if (!Text.IsEmpty())
	{
		OnStatus.Broadcast(Text, bError);
	}
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
	StopLan();
	bMatchRunning = false;
	bSearching = false;
	EosSearch = EEosSearch::None;
	if (bOnlineLobby)
	{
		IHCEos::Get().DestroyLobby();
		bOnlineLobby = false;
	}
	FRandomStream Random(static_cast<int32>(FDateTime::Now().GetTicks() & 0x7fffffff) ^ static_cast<int32>(FPlatformTime::Cycles()));
	PendingCode = FHCJoinCode::Make(Random);

	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		// No online subsystem: still allow LAN play by IP
		Report(NSLOCTEXT("HC", "NoOnline", "Kein Online-System, Beitritt nur per IP."), false);
		JoinCode = FString();
		IHCEos::UseP2PForGame(false);
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
	if (!bWasSuccessful)
	{
		bBusy = false;
		Report(NSLOCTEXT("HC", "CreateFailed", "Lobby konnte nicht erstellt werden."), true);
		return;
	}
	// The local session keeps the engine's voice chat running in both cases (online and LAN)
	JoinCode = PendingCode;
	ContinueHosting();
}

void UHCSessionSubsystem::ContinueHosting()
{
	if (IHCEos::Get().GetState() == EHCEosState::LoggingIn && !bHostWaitingForEos)
	{
		// Clicked right after the start: give the EOS login a few seconds (TickLan calls back)
		bHostWaitingForEos = true;
		HostWaitForEosUntil = FPlatformTime::Seconds() + 8.0;
		Report(NSLOCTEXT("HC", "OnlineConnecting", "Verbinde mit dem Online-Dienst ..."), false);
		return;
	}
	bHostWaitingForEos = false;
	if (!IHCEos::Get().IsReady())
	{
		HostOverLan();
		return;
	}
	Report(NSLOCTEXT("HC", "HostingOnline", "Online-Lobby wird erstellt ..."), false);
	TWeakObjectPtr<UHCSessionSubsystem> WeakThis(this);
	const FString Code = JoinCode;
	IHCEos::Get().CreateLobby(Code, GetBuildVersion(), UHCGameSettings::Get()->MaxPlayers, [WeakThis, Code](bool bOk)
	{
		UHCSessionSubsystem* Self = WeakThis.Get();
		if (!Self || !Self->bBusy || Self->JoinCode != Code)
		{
			if (bOk)
			{
				IHCEos::Get().DestroyLobby(); // hosting was cancelled meanwhile
			}
			return;
		}
		if (!bOk)
		{
			Self->Report(NSLOCTEXT("HC", "OnlineLobbyFailed", "Online-Lobby fehlgeschlagen: nur Heimnetz (LAN/IP)."), true);
			Self->HostOverLan();
			return;
		}
		Self->bBusy = false;
		Self->bOnlineLobby = true;
		IHCEos::UseP2PForGame(true);
		UE_LOG(LogHCNet, Log, TEXT("Hosting online lobby with join code %s (EOS P2P)"), *Code);
		Self->OpenLobbyAsHost();
	});
}

void UHCSessionSubsystem::HostOverLan()
{
	bBusy = false;
	bOnlineLobby = false;
	IHCEos::UseP2PForGame(false);
	UE_LOG(LogHCNet, Log, TEXT("Hosting lobby with join code %s, LAN address %s"), *JoinCode, *GetLocalAddress());
	LanResponder = MakeUnique<FHCLanResponder>();
	if (!LanResponder->Start())
	{
		LanResponder.Reset();
		UE_LOG(LogHCNet, Warning, TEXT("LAN search port %d cannot be opened: others can only join by IP address"), HCLanDiscovery::Port);
	}
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
	StopLan();
	DestroyVoiceOnlySession();
	if (FHCJoinCode::LooksLikeAddress(CodeOrAddress))
	{
		Report(FText::Format(NSLOCTEXT("HC", "ConnectingIp", "Verbinde mit {0} ..."), FText::FromString(CodeOrAddress)), false);
		CreateVoiceOnlySession();
		IHCEos::UseP2PForGame(false);
		TravelToAddress(CodeOrAddress.TrimStartAndEnd());
		return;
	}
	FString Code;
	if (!FHCJoinCode::Normalize(CodeOrAddress, Code))
	{
		Report(NSLOCTEXT("HC", "BadCode", "Ungültiger Code. 6 Zeichen, ohne 0, O, 1, I und L. Oder eine IP-Adresse."), true);
		return;
	}
	PendingCode = Code;
	bBusy = true;
	bSearching = true;
	++JoinAttempt;
	Report(FText::Format(NSLOCTEXT("HC", "Searching", "Suche Lobby {0} ..."), FText::FromString(FHCJoinCode::Format(Code))), false);

	// Both at once: the LAN (hosts without EOS) and EOS (hosts anywhere); the first answer wins
	const double Now = FPlatformTime::Seconds();
	LanSearch = MakeUnique<FHCLanSearch>();
	if (!LanSearch->Start(Code))
	{
		LanSearch.Reset();
	}
	LanSearchDeadline = Now + HCLanDiscovery::SearchSeconds;
	EosSearch = EEosSearch::None;
	switch (IHCEos::Get().GetState())
	{
	case EHCEosState::Ready:
		StartEosSearch();
		break;
	case EHCEosState::LoggingIn:
		EosSearch = EEosSearch::WaitingForLogin;
		EosSearchWaitUntil = Now + 8.0;
		break;
	default:
		break;
	}
	if (!LanSearch && EosSearch == EEosSearch::None)
	{
		bBusy = false;
		bSearching = false;
		Report(NSLOCTEXT("HC", "SearchFailed", "Suche fehlgeschlagen."), true);
	}
}

void UHCSessionSubsystem::StartEosSearch()
{
	EosSearch = EEosSearch::Running;
	TWeakObjectPtr<UHCSessionSubsystem> WeakThis(this);
	const int32 Attempt = JoinAttempt;
	IHCEos::Get().FindLobby(PendingCode, [WeakThis, Attempt](EHCEosFind Result, const FHCEosLobby& Lobby)
	{
		if (UHCSessionSubsystem* Self = WeakThis.Get())
		{
			Self->HandleEosFind(Result, Lobby, Attempt);
		}
	});
}

void UHCSessionSubsystem::HandleEosFind(EHCEosFind Result, const FHCEosLobby& Lobby, int32 Attempt)
{
	if (Attempt != JoinAttempt || !bSearching || EosSearch != EEosSearch::Running)
	{
		return; // old attempt, or the LAN answered first
	}
	EosSearch = EEosSearch::Done;
	if (Result != EHCEosFind::Found)
	{
		UE_LOG(LogHCNet, Log, TEXT("Lobby %s not found online%s"), *PendingCode, Result == EHCEosFind::Failed ? TEXT(" (search failed)") : TEXT(""));
		return; // the LAN search may still answer; TickLan reports "not found" when both are done
	}
	LanSearch.Reset();
	bSearching = false;
	bBusy = false;
	if (!CheckFoundLobby(Lobby.BuildVersion, Lobby.bMatchRunning))
	{
		return;
	}
	UE_LOG(LogHCNet, Log, TEXT("Lobby %s found online, host %s"), *PendingCode, *Lobby.TravelAddress);
	JoinFoundLobby(Lobby.TravelAddress, true);
}

bool UHCSessionSubsystem::CheckFoundLobby(const FString& HostVersion, bool bLobbyMatchRunning)
{
	if (!HostVersion.IsEmpty() && HostVersion != GetBuildVersion())
	{
		Report(FText::Format(NSLOCTEXT("HC", "VersionMismatch", "Der Host hat Version {0}, du hast {1}. Starte den Launcher neu."),
			FText::FromString(HostVersion), FText::FromString(GetBuildVersion())), true);
		return false;
	}
	if (bLobbyMatchRunning)
	{
		Report(NSLOCTEXT("HC", "MatchRunning", "Runde läuft gerade, versuch es nach der Runde."), true);
		return false;
	}
	return true;
}

void UHCSessionSubsystem::JoinFoundLobby(const FString& Address, bool bOverEos)
{
	// Joined like an address: a stale game session goes, a local one keeps the voice chat running
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		if (Sessions->GetNamedSession(NAME_GameSession))
		{
			Sessions->DestroySession(NAME_GameSession);
		}
	}
	Report(NSLOCTEXT("HC", "Joining", "Lobby gefunden, trete bei ..."), false);
	JoinCode = PendingCode;
	CreateVoiceOnlySession();
	IHCEos::UseP2PForGame(bOverEos);
	TravelToAddress(Address);
}

bool UHCSessionSubsystem::TickLan(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (bHostWaitingForEos && (IHCEos::Get().GetState() != EHCEosState::LoggingIn || Now >= HostWaitForEosUntil))
	{
		ContinueHosting();
	}
	if (LanSearch)
	{
		HCLanDiscovery::FAnswer Answer;
		FString HostIp;
		if (LanSearch->Poll(Now, Answer, HostIp))
		{
			LanSearch.Reset();
			HandleLanAnswer(Answer, HostIp);
		}
		else if (Now >= LanSearchDeadline)
		{
			LanSearch.Reset();
		}
	}
	if (bSearching && EosSearch == EEosSearch::WaitingForLogin)
	{
		const EHCEosState State = IHCEos::Get().GetState();
		if (State == EHCEosState::Ready)
		{
			StartEosSearch();
		}
		else if (State != EHCEosState::LoggingIn || Now >= EosSearchWaitUntil)
		{
			EosSearch = EEosSearch::Done;
		}
	}
	if (bSearching && !LanSearch && EosSearch != EEosSearch::Running && EosSearch != EEosSearch::WaitingForLogin)
	{
		bSearching = false;
		bBusy = false;
		const EHCEosState State = IHCEos::Get().GetState();
		if (State == EHCEosState::Ready)
		{
			Report(NSLOCTEXT("HC", "NotFoundOnline", "Keine Lobby mit diesem Code gefunden. Stimmt der Code, und ist die Lobby des Hosts noch offen?"), true);
		}
		else if (State == EHCEosState::Off)
		{
			Report(NSLOCTEXT("HC", "NotFound", "Keine Lobby mit diesem Code im Netzwerk gefunden. Tipp: per IP-Adresse des Hosts beitreten."), true);
		}
		else
		{
			Report(NSLOCTEXT("HC", "NotFoundLanOnly", "Keine Lobby mit diesem Code im Heimnetz gefunden, online gerade nicht verfügbar. Tipp: per IP-Adresse des Hosts beitreten."), true);
		}
	}
	if (LanResponder && !JoinCode.IsEmpty())
	{
		// Only while the lobby or round runs as listen server (not during a map change); searches resend anyway
		UWorld* World = GetWorld();
		UNetDriver* Driver = World && World->GetNetMode() == NM_ListenServer ? World->GetNetDriver() : nullptr;
		TSharedPtr<const FInternetAddr> GameAddr;
		if (Driver)
		{
			GameAddr = Driver->GetLocalAddr();
		}
		if (GameAddr.IsValid() && GameAddr->GetPort() > 0)
		{
			HCLanDiscovery::FAnswer Answer;
			Answer.Code = JoinCode;
			Answer.BuildVersion = GetBuildVersion();
			Answer.bMatchRunning = bMatchRunning;
			Answer.GamePort = GameAddr->GetPort();
			LanResponder->Poll(Answer);
		}
	}
	return true;
}

void UHCSessionSubsystem::HandleLanAnswer(const HCLanDiscovery::FAnswer& Answer, const FString& HostIp)
{
	bBusy = false;
	bSearching = false;
	EosSearch = EEosSearch::None; // a late EOS answer is ignored
	if (!CheckFoundLobby(Answer.BuildVersion, Answer.bMatchRunning))
	{
		return;
	}
	const FString Address = FString::Printf(TEXT("%s:%d"), *HostIp, Answer.GamePort);
	UE_LOG(LogHCNet, Log, TEXT("Lobby %s found at %s"), *PendingCode, *Address);
	JoinFoundLobby(Address, false);
}

void UHCSessionSubsystem::StopLan()
{
	LanSearch.Reset();
	LanResponder.Reset();
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
	bMatchRunning = bRunning;
	if (bOnlineLobby)
	{
		IHCEos::Get().SetLobbyMatchRunning(bRunning);
	}
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
	StopLan();
	if (bOnlineLobby)
	{
		IHCEos::Get().DestroyLobby();
		bOnlineLobby = false;
	}
	bBusy = false;
	bSearching = false;
	bHostWaitingForEos = false;
	EosSearch = EEosSearch::None;
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
