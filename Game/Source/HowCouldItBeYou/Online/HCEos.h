#pragma once

#include "CoreMinimal.h"

/**
 * Epic Online Services for playing over the internet (docs/03 §6–7, milestone M9).
 *
 * The game talks to the EOS SDK itself instead of going through OnlineSubsystemEOS: that plugin cannot log in
 * by Device ID without an Epic account (it sends no display name and never creates the device ID), and a patched
 * copy of engine code could not live in the repository. What it uses from the engine:
 *  - EOSShared: creates the EOS platform from the ini section [EOSSDK.Platform.HCDev] and ticks it,
 *  - SocketSubsystemEOS: NetDriverEOS, the P2P game connection with NAT traversal and Epic's relays.
 *
 * Login: EOS Connect with a Device ID (anonymous, bound to the Windows user, no Epic account).
 * Lobby: an EOS lobby per hosted game with the attributes JOINCODE, BUILDVER and STATE (LOBBY/MATCH);
 * the lobby owner's ProductUserId is the address friends connect to ("EOS:<puid>").
 *
 * Windows only: Epic ships SocketSubsystemEOS for Win64, Mac and Android, not for Linux (HC_WITH_EOS=0 there).
 * Off in the editor and with -HCOnline=Null; then everything runs over LAN and IP as before.
 */
enum class EHCEosState : uint8
{
	/** Not in this build, in the editor, or switched off on the command line. */
	Off,
	/** The ini has no EOS credentials (no ClientSecret in Game/Config/Windows/WindowsEngine.ini). */
	NoCredentials,
	LoggingIn,
	Ready,
	Failed
};

/** What a join-code search found. */
struct FHCEosLobby
{
	/** Address for ClientTravel, "[EOS:<puid>]" (the brackets keep FURL from taking "EOS" for a protocol). */
	FString TravelAddress;
	FString BuildVersion;
	bool bMatchRunning = false;
};

enum class EHCEosFind : uint8
{
	Found,
	NotFound,
	Failed
};

class IHCEos
{
public:
	virtual ~IHCEos() = default;

	/** The process-wide instance (the EOS platform and the P2P sockets live as long as the process). */
	static IHCEos& Get();

	/** Logs in (once; again after a lost connection). DisplayName is shown nowhere in the game, EOS just wants one. */
	virtual void Start(const FString& DisplayName) = 0;
	virtual EHCEosState GetState() const = 0;
	/** Short reason for NoCredentials/Failed, for the log and the main menu (never contains secrets). */
	virtual FString GetError() const = 0;
	bool IsReady() const { return GetState() == EHCEosState::Ready; }

	/** Host: creates the lobby that friends find by code. Done(false) if anything failed. */
	virtual void CreateLobby(const FString& Code, const FString& BuildVersion, int32 MaxMembers, TFunction<void(bool)> Done) = 0;
	/** Host: STATE=MATCH while a round runs. */
	virtual void SetLobbyMatchRunning(bool bRunning) = 0;
	/** Host: removes the lobby (leaving, or hosting again). */
	virtual void DestroyLobby() = 0;
	virtual bool HasLobby() const = 0;

	/** Client: one search by join code (EOS allows about 30 per minute). */
	virtual void FindLobby(const FString& Code, TFunction<void(EHCEosFind, const FHCEosLobby&)> Done) = 0;

	/** Called whenever the state changes (game thread). */
	TMulticastDelegate<void(EHCEosState)> OnStateChanged;

	/**
	 * Which driver the next listen/connect uses: NetDriverEOS (P2P) for online games, the plain IpNetDriver for LAN
	 * and IP joins, so those behave exactly as before M9. Seamless travel keeps the driver of the running game.
	 */
	static void UseP2PForGame(bool bP2P);
};
