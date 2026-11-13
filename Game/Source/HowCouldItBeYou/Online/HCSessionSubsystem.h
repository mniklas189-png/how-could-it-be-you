#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Online/HCEos.h"
#include "Online/HCLanDiscovery.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HCSessionSubsystem.generated.h"

/** Status for the main menu: text + whether it is an error. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FHCOnSessionStatus, const FText& /*Message*/, bool /*bIsError*/);

/**
 * Hosting and joining (docs/03 §7). The menus only talk to this class, never to the online subsystem.
 * Stage 1 (M2): OnlineSubsystemNull session (the voice chat needs one), lobbies found by join code through the game's
 * own LAN search (HCLanDiscovery, since 0.11.8; the engine's beacon missed PCs with virtual adapters), plus direct IP connect.
 * Stage 3 (M9, since 0.12.0): over the internet with EOS (Online/HCEos): Device ID login at start, the host opens an
 * EOS lobby with JOINCODE/BUILDVER/STATE and listens over EOS P2P; a join by code searches EOS and the LAN at the same
 * time and takes whatever answers first. Without EOS (Linux, editor, no credentials, login failed) everything stays LAN/IP.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Creates a session with a fresh join code and opens the lobby map as listen server. */
	void HostLobby(const FString& PlayerName);

	/** Accepts a join code ("K7M 4QX") or a LAN address ("192.168.0.5"). */
	void Join(const FString& CodeOrAddress, const FString& PlayerName);

	/** Client: leave and go to the main menu. Host: tell everyone, end the session, go to the main menu. */
	void LeaveToMainMenu();

	/** Destroys the session without travelling (after a network failure). */
	void DestroySessionQuietly();

	/** Host: advertise STATE=MATCH while a round runs, STATE=LOBBY otherwise. */
	void SetMatchRunning(bool bRunning);

	FString GetJoinCode() const { return JoinCode; }
	bool IsBusy() const { return bBusy; }

	/** Version every client must match ("0.2.0", "dev" in the editor), docs/03 §7.4. */
	static FString GetBuildVersion();

	/** First non-loopback IPv4 of this PC, for "join by IP" in the LAN. */
	static FString GetLocalAddress();

	/** Removes characters that would break the travel URL. */
	static FString SanitizePlayerName(const FString& Name);

	/** Host: this lobby is reachable over the internet (EOS), so the IP address is not offered. */
	bool IsOnlineLobby() const { return bOnlineLobby; }

	/** One line for the main menu about online play (empty when EOS is off in this build). */
	FText GetOnlineStatus(bool& bOutError) const;

	FHCOnSessionStatus OnStatus;

private:
	IOnlineSessionPtr GetSessions() const;
	void CreateSessionNow();
	void OpenLobbyAsHost();
	void TravelToAddress(const FString& Address);
	void Report(const FText& Message, bool bError);
	/**
	 * Joining by address skips the online session, but the engine's voice chat does nothing without one
	 * (FOnlineVoiceImpl::Tick and the talker registration ask GetNumSessions() > 0): nobody would hear or be heard.
	 * Such a player gets a local session on their own machine instead, never advertised (no LAN beacon).
	 */
	void CreateVoiceOnlySession();
	void DestroyVoiceOnlySession();

	void HandleDestroyBeforeHost(FName SessionName, bool bWasSuccessful);
	void HandleCreateSession(FName SessionName, bool bWasSuccessful);
	/** Host, after the local session exists: EOS lobby if online play is ready, else LAN. */
	void ContinueHosting();
	void HostOverLan();
	void HandleEosState(EHCEosState State);
	void StartEosSearch();
	void HandleEosFind(EHCEosFind Result, const FHCEosLobby& Lobby, int32 Attempt);
	/** Version and STATE of a found lobby; reports and returns false if joining makes no sense. */
	bool CheckFoundLobby(const FString& HostVersion, bool bMatchRunning);
	/** Ends a search attempt (LAN and EOS) and connects. */
	void JoinFoundLobby(const FString& Address, bool bOverEos);
	/** Host: answer LAN searches for the join code. Client: run the search. Every frame. */
	bool TickLan(float DeltaTime);
	void HandleLanAnswer(const HCLanDiscovery::FAnswer& Answer, const FString& HostIp);
	void StopLan();
	void FinishHostLeave();

	TUniquePtr<FHCLanResponder> LanResponder;
	TUniquePtr<FHCLanSearch> LanSearch;
	double LanSearchDeadline = 0.0;
	FTSTicker::FDelegateHandle LanTicker;
	FString JoinCode;
	FString PendingCode;
	FString PendingName;
	bool bBusy = false;
	/** Host: the lobby is an EOS lobby (P2P), not a LAN one. */
	bool bOnlineLobby = false;
	/** Host clicked while the EOS login was still running: wait for it until this time. */
	double HostWaitForEosUntil = 0.0;
	bool bHostWaitingForEos = false;
	/** Join by code: the EOS part of the search. */
	enum class EEosSearch : uint8 { None, WaitingForLogin, Running, Done };
	EEosSearch EosSearch = EEosSearch::None;
	double EosSearchWaitUntil = 0.0;
	/** Counts join attempts, so a late EOS answer of an old attempt is ignored. */
	int32 JoinAttempt = 0;
	/** A join by code is searching (LAN and/or EOS). */
	bool bSearching = false;
	FDelegateHandle EosStateHandle;
	/** Host: a round runs (LAN answers say MATCH). */
	bool bMatchRunning = false;

	FDelegateHandle CreateHandle;
	FDelegateHandle DestroyHandle;
	FTimerHandle LeaveTimer;
};
