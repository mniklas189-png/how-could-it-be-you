#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HCSessionSubsystem.generated.h"

class FOnlineSessionSearch;

/** Status for the main menu: text + whether it is an error. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FHCOnSessionStatus, const FText& /*Message*/, bool /*bIsError*/);

/**
 * Hosting and joining (docs/03 §7). The menus only talk to this class, never to the online subsystem.
 * Stage 1 (M2): OnlineSubsystemNull = LAN sessions found by join code, plus direct IP connect.
 * Stage 3 (M9): EOS behind the same functions.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
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

	FHCOnSessionStatus OnStatus;

private:
	IOnlineSessionPtr GetSessions() const;
	void CreateSessionNow();
	void OpenLobbyAsHost();
	void TravelToAddress(const FString& Address);
	void Report(const FText& Message, bool bError);

	void HandleDestroyBeforeHost(FName SessionName, bool bWasSuccessful);
	void HandleCreateSession(FName SessionName, bool bWasSuccessful);
	void HandleFindSessions(bool bWasSuccessful);
	void HandleJoinSession(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void FinishHostLeave();

	TSharedPtr<FOnlineSessionSearch> Search;
	FString JoinCode;
	FString PendingCode;
	FString PendingName;
	bool bBusy = false;

	FDelegateHandle CreateHandle;
	FDelegateHandle DestroyHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FTimerHandle LeaveTimer;
};
