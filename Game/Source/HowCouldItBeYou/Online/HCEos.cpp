#include "Online/HCEos.h"

#include "Core/HCLog.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Online/HCEosTunnel.h"

namespace HCEosConfig
{
	/** Ini section read by EOSShared: [EOSSDK.Platform.HCDev] (IDs in DefaultEngine.ini, ClientSecret in WindowsEngine.ini). */
	const TCHAR* const PlatformName = TEXT("HCDev");
	const TCHAR* const Section = TEXT("EOSSDK.Platform.HCDev");
	/** EOS P2P socket of the game tunnel (HCEosTunnel.h); only these packets are read. */
	const char* const TunnelSocketName = "HCGame";
	constexpr uint8 TunnelChannel = 0;
	/** Upper bound of P2P packets read per frame, so a flood cannot stall a frame. */
	constexpr int32 MaxPacketsPerTick = 1024;
}

#if HC_WITH_EOS

#include "IEOSSDKManager.h"
#include "Containers/Ticker.h"
#include "EOSShared.h"
#include "EOSSharedTypes.h"
#include "Misc/CoreDelegates.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_lobby.h"
#include "eos_p2p.h"

/** Named namespace: unity builds put several .cpp files into one translation unit (HCSessionSubsystem.cpp has its own keys). */
namespace HCEosDetail
{
	const char* const KeyJoinCode = "JOINCODE";
	const char* const KeyBuildVer = "BUILDVER";
	const char* const KeyState = "STATE";

	/** One-shot EOS callback holding a lambda; deleted after the final call (EOS may call again with "will retry"). */
	template <typename TInfo>
	struct THCEosCallback
	{
		TFunction<void(const TInfo*)> Fn;

		static void EOS_CALL Call(const TInfo* Data)
		{
			if (!EOS_EResult_IsOperationComplete(Data->ResultCode))
			{
				return;
			}
			THCEosCallback* Self = static_cast<THCEosCallback*>(Data->ClientData);
			Self->Fn(Data);
			delete Self;
		}
	};

	template <typename TInfo>
	THCEosCallback<TInfo>* MakeCallback(TFunction<void(const TInfo*)> Fn)
	{
		return new THCEosCallback<TInfo>{ MoveTemp(Fn) };
	}

	FString ReadStringAttribute(EOS_HLobbyDetails Details, const char* Key)
	{
		EOS_LobbyDetails_CopyAttributeByKeyOptions Options = {};
		Options.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;
		Options.AttrKey = Key;
		EOS_Lobby_Attribute* Attribute = nullptr;
		FString Value;
		if (EOS_LobbyDetails_CopyAttributeByKey(Details, &Options, &Attribute) == EOS_EResult::EOS_Success && Attribute)
		{
			if (Attribute->Data && Attribute->Data->ValueType == EOS_EAttributeType::EOS_AT_STRING && Attribute->Data->Value.AsUtf8)
			{
				Value = UTF8_TO_TCHAR(Attribute->Data->Value.AsUtf8);
			}
			EOS_Lobby_Attribute_Release(Attribute);
		}
		return Value;
	}

	FString ResultText(EOS_EResult Result)
	{
		return LexToString(Result);
	}
}


class FHCEos : public IHCEos, public IHCPeerLink
{
public:
	virtual void Start(const FString& DisplayName) override;
	virtual EHCEosState GetState() const override { return State; }
	virtual FString GetError() const override { return Error; }
	virtual void CreateLobby(const FString& Code, const FString& BuildVersion, int32 MaxMembers, TFunction<void(bool)> Done) override;
	virtual void SetLobbyMatchRunning(bool bRunning) override;
	virtual void DestroyLobby() override;
	virtual bool HasLobby() const override { return !LobbyId.IsEmpty(); }
	virtual void FindLobby(const FString& Code, TFunction<void(EHCEosFind, const FHCEosLobby&)> Done) override;
	virtual void ServeOnlinePlayers(int32 GamePort) override;
	virtual void StopServingOnlinePlayers() override;
	virtual FString OpenTunnelToHost(const FString& HostId) override;
	virtual void CloseTunnelToHost() override;

	// IHCPeerLink
	virtual bool SendToPeer(const FString& PeerId, const uint8* Data, int32 Size) override;

private:
	bool CreatePlatform();
	void SetState(EHCEosState NewState, const FString& NewError = FString());
	void CreateDeviceId();
	void Login();
	void CreateUser(EOS_ContinuanceToken Token);
	void LoginDone(EOS_ProductUserId UserId);
	void LoginFailed(const FString& Reason);
	void RetryLoginLater();
	void WriteLobbyAttributes(const TArray<TPair<FString, FString>>& Attributes, TFunction<void(bool)> Done);

	/** P2P notifications for the logged-in user (again after a login with another ProductUserId). */
	void BindP2P();
	void UnbindP2P();
	/** Moves the tunnels' packets: P2P in, local UDP out and back. Start and end of every frame. */
	void TickTunnels();
	EOS_ProductUserId PeerUserId(const FString& PeerId);
	void ClosePeerConnection(const FString& PeerId);

	static void EOS_CALL HandleAuthExpiration(const EOS_Connect_AuthExpirationCallbackInfo* Data);
	static void EOS_CALL HandleLoginStatusChanged(const EOS_Connect_LoginStatusChangedCallbackInfo* Data);
	static void EOS_CALL HandleConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* Data);
	static void EOS_CALL HandleConnectionEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* Data);
	static void EOS_CALL HandleConnectionInterrupted(const EOS_P2P_OnPeerConnectionInterruptedInfo* Data);
	static void EOS_CALL HandleConnectionClosed(const EOS_P2P_OnRemoteConnectionClosedInfo* Data);
	static void EOS_CALL HandleNatType(const EOS_P2P_OnQueryNATTypeCompleteInfo* Data);

	EHCEosState State = EHCEosState::Off;
	FString Error;
	FString DisplayNameUtf;
	bool bPlatformTried = false;

	IEOSPlatformHandlePtr Platform;
	EOS_HConnect Connect = nullptr;
	EOS_HLobby Lobby = nullptr;
	EOS_HP2P P2P = nullptr;
	FTSTicker::FDelegateHandle RetryTicker;
	FTSTicker::FDelegateHandle TunnelTicker;

	EOS_ProductUserId LocalUserId = nullptr;
	FString LobbyId;
#if !UE_BUILD_SHIPPING
	bool bNewIdentityDone = false;
#endif

	EOS_P2P_SocketId TunnelSocket = {};
	/** User the P2P notifications are bound to. */
	EOS_ProductUserId P2PBoundUser = nullptr;
	EOS_NotificationId RequestNotify = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId EstablishedNotify = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId InterruptedNotify = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId ClosedNotify = EOS_INVALID_NOTIFICATIONID;
	TMap<FString, EOS_ProductUserId> PeerIds;
	/** Peers a send failed for, so the log gets one line per peer. */
	TSet<FString> SendFailedPeers;
	TUniquePtr<FHCTunnelHost> HostTunnel;
	TUniquePtr<FHCTunnelClient> ClientTunnel;
};

IHCEos& IHCEos::Get()
{
	// Never destroyed: the EOS platform and the tunnels' sockets live until the process ends
	static FHCEos* Instance = new FHCEos();
	return *Instance;
}

void FHCEos::SetState(EHCEosState NewState, const FString& NewError)
{
	State = NewState;
	Error = NewError;
	OnStateChanged.Broadcast(State);
}

bool FHCEos::CreatePlatform()
{
	if (bPlatformTried)
	{
		return Platform.IsValid();
	}
	bPlatformTried = true;

	if (GIsEditor)
	{
		return false; // PIE always plays over LAN (docs/03 §7.2)
	}
	FString Mode;
	if (FParse::Value(FCommandLine::Get(), TEXT("HCOnline="), Mode) && Mode.Equals(TEXT("Null"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogHCNet, Log, TEXT("EOS off (-HCOnline=Null): LAN and IP only"));
		return false;
	}
#if !UE_BUILD_SHIPPING
	// Smoke tests start host and clients on one PC: one Windows user = one EOS identity, P2P to yourself fails
	if (!Mode.Equals(TEXT("EOS"), ESearchCase::IgnoreCase)
		&& (FParse::Param(FCommandLine::Get(), TEXT("HCAutoHost")) || FCString::Stristr(FCommandLine::Get(), TEXT("HCAutoJoinCodeFile=")) != nullptr))
	{
		UE_LOG(LogHCNet, Log, TEXT("EOS off for the smoke test (add -HCOnline=EOS to use it)"));
		return false;
	}
#endif

	auto Missing = [](const TCHAR* Key)
	{
		FString Value;
		GConfig->GetString(HCEosConfig::Section, Key, Value, GEngineIni);
		Value.TrimStartAndEndInline();
		return Value.IsEmpty() || Value.Contains(TEXT("HIER_EINTRAGEN"));
	};
	if (Missing(TEXT("ProductId")) || Missing(TEXT("SandboxId")) || Missing(TEXT("DeploymentId")) || Missing(TEXT("ClientId")) || Missing(TEXT("ClientSecret")))
	{
		UE_LOG(LogHCNet, Error, TEXT("EOS-Zugangsdaten fehlen, siehe docs/03 §6 (Tools/SetupEOS.bat): only LAN and IP"));
		SetState(EHCEosState::NoCredentials, TEXT("no credentials"));
		return false;
	}

	IEOSSDKManager* Manager = IEOSSDKManager::Get();
	if (!Manager || !Manager->IsInitialized())
	{
		UE_LOG(LogHCNet, Error, TEXT("EOS SDK not initialized: only LAN and IP"));
		SetState(EHCEosState::Failed, TEXT("EOS SDK missing"));
		return false;
	}
	Platform = Manager->CreatePlatform(HCEosConfig::PlatformName);
	if (!Platform.IsValid())
	{
		UE_LOG(LogHCNet, Error, TEXT("EOS platform could not be created (check the IDs in [%s]): only LAN and IP"), HCEosConfig::Section);
		SetState(EHCEosState::Failed, TEXT("EOS platform"));
		return false;
	}
	Connect = EOS_Platform_GetConnectInterface(*Platform);
	Lobby = EOS_Platform_GetLobbyInterface(*Platform);

	EOS_Connect_AddNotifyAuthExpirationOptions ExpirationOptions = {};
	ExpirationOptions.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
	EOS_Connect_AddNotifyAuthExpiration(Connect, &ExpirationOptions, this, &FHCEos::HandleAuthExpiration);
	EOS_Connect_AddNotifyLoginStatusChangedOptions StatusOptions = {};
	StatusOptions.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
	EOS_Connect_AddNotifyLoginStatusChanged(Connect, &StatusOptions, this, &FHCEos::HandleLoginStatusChanged);

	// Game tunnel over P2P (HCEosTunnel.h): read at the start of a frame, before the net driver; sent at its end,
	// right after the net driver wrote its packets
	P2P = EOS_Platform_GetP2PInterface(*Platform);
	TunnelSocket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
	FCStringAnsi::Strncpy(TunnelSocket.SocketName, HCEosConfig::TunnelSocketName, EOS_P2P_SOCKETID_SOCKETNAME_SIZE);
	TunnelTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		TickTunnels();
		return true;
	}), 0.f);
	FCoreDelegates::OnEndFrame.AddRaw(this, &FHCEos::TickTunnels);
	UE_LOG(LogHCNet, Log, TEXT("EOS platform ready (SDK %s)"), *Manager->GetSDKVersion());
	return true;
}

void FHCEos::Start(const FString& DisplayName)
{
	if (State == EHCEosState::LoggingIn || State == EHCEosState::Ready)
	{
		return;
	}
	DisplayNameUtf = DisplayName.IsEmpty() ? FString(TEXT("Forscher")) : DisplayName.Left(EOS_CONNECT_USERLOGININFO_DISPLAYNAME_MAX_LENGTH);
	if (!CreatePlatform())
	{
		return;
	}
	SetState(EHCEosState::LoggingIn);
	UE_LOG(LogHCNet, Log, TEXT("EOS login (Device ID) ..."));
#if !UE_BUILD_SHIPPING
	// Test with two online identities on one PC (one Windows user = one device ID): the second game replaces this
	// PC's device ID by a new one after the first game has logged in. Nothing is stored per identity.
	if (!bNewIdentityDone && FParse::Param(FCommandLine::Get(), TEXT("HCEosNewIdentity")))
	{
		bNewIdentityDone = true;
		UE_LOG(LogHCNet, Warning, TEXT("EOS: -HCEosNewIdentity, this PC gets a new device ID"));
		EOS_Connect_DeleteDeviceIdOptions Options = {};
		Options.ApiVersion = EOS_CONNECT_DELETEDEVICEID_API_LATEST;
		EOS_Connect_DeleteDeviceId(Connect, &Options, HCEosDetail::MakeCallback<EOS_Connect_DeleteDeviceIdCallbackInfo>([this](const EOS_Connect_DeleteDeviceIdCallbackInfo* Data)
		{
			UE_LOG(LogHCNet, Log, TEXT("EOS device ID deleted: %s"), *HCEosDetail::ResultText(Data->ResultCode));
			CreateDeviceId();
		}), &HCEosDetail::THCEosCallback<EOS_Connect_DeleteDeviceIdCallbackInfo>::Call);
		return;
	}
#endif
	if (LocalUserId)
	{
		Login();
	}
	else
	{
		CreateDeviceId();
	}
}

void FHCEos::CreateDeviceId()
{
	EOS_Connect_CreateDeviceIdOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	Options.DeviceModel = PLATFORM_WINDOWS ? "PC Windows" : "PC";
	EOS_Connect_CreateDeviceId(Connect, &Options, HCEosDetail::MakeCallback<EOS_Connect_CreateDeviceIdCallbackInfo>([this](const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
	{
		// "Already exists" is the normal case from the second start on
		if (Data->ResultCode == EOS_EResult::EOS_Success || Data->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed)
		{
			Login();
		}
		else
		{
			LoginFailed(FString::Printf(TEXT("CreateDeviceId: %s"), *HCEosDetail::ResultText(Data->ResultCode)));
		}
	}), &HCEosDetail::THCEosCallback<EOS_Connect_CreateDeviceIdCallbackInfo>::Call);
}

void FHCEos::Login()
{
	EOS_Connect_Credentials Credentials = {};
	Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
	Credentials.Token = nullptr; // Device ID: no token

	const FTCHARToUTF8 NameUtf8(*DisplayNameUtf);
	EOS_Connect_UserLoginInfo LoginInfo = {};
	LoginInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
	LoginInfo.DisplayName = NameUtf8.Get();

	EOS_Connect_LoginOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	Options.Credentials = &Credentials;
	Options.UserLoginInfo = &LoginInfo;
	EOS_Connect_Login(Connect, &Options, HCEosDetail::MakeCallback<EOS_Connect_LoginCallbackInfo>([this](const EOS_Connect_LoginCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			LoginDone(Data->LocalUserId);
		}
		else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken)
		{
			CreateUser(Data->ContinuanceToken); // first start on this PC: create the EOS user for the device ID
		}
		else
		{
			LoginFailed(FString::Printf(TEXT("Login: %s"), *HCEosDetail::ResultText(Data->ResultCode)));
		}
	}), &HCEosDetail::THCEosCallback<EOS_Connect_LoginCallbackInfo>::Call);
}

void FHCEos::CreateUser(EOS_ContinuanceToken Token)
{
	EOS_Connect_CreateUserOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
	Options.ContinuanceToken = Token;
	EOS_Connect_CreateUser(Connect, &Options, HCEosDetail::MakeCallback<EOS_Connect_CreateUserCallbackInfo>([this](const EOS_Connect_CreateUserCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			LoginDone(Data->LocalUserId);
		}
		else
		{
			LoginFailed(FString::Printf(TEXT("CreateUser: %s"), *HCEosDetail::ResultText(Data->ResultCode)));
		}
	}), &HCEosDetail::THCEosCallback<EOS_Connect_CreateUserCallbackInfo>::Call);
}

void FHCEos::LoginDone(EOS_ProductUserId UserId)
{
	LocalUserId = UserId;
	UE_LOG(LogHCNet, Log, TEXT("EOS login ok, online play ready (PUID %s)"), *LexToString(UserId));
	BindP2P();
	SetState(EHCEosState::Ready);
}

void FHCEos::LoginFailed(const FString& Reason)
{
	UE_LOG(LogHCNet, Warning, TEXT("EOS login failed (%s): only LAN and IP for now, next try in 30 s"), *Reason);
	SetState(EHCEosState::Failed, Reason);
	RetryLoginLater();
}

void FHCEos::RetryLoginLater()
{
	if (RetryTicker.IsValid())
	{
		return;
	}
	RetryTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		RetryTicker.Reset();
		if (State == EHCEosState::Failed)
		{
			Start(DisplayNameUtf);
		}
		return false;
	}), 30.f);
}

void EOS_CALL FHCEos::HandleAuthExpiration(const EOS_Connect_AuthExpirationCallbackInfo* Data)
{
	// The access token lasts about an hour; EOS warns ~10 min before. Same Device ID login again.
	FHCEos* Self = static_cast<FHCEos*>(Data->ClientData);
	UE_LOG(LogHCNet, Log, TEXT("EOS token expires soon, renewing"));
	Self->Login();
}

void EOS_CALL FHCEos::HandleLoginStatusChanged(const EOS_Connect_LoginStatusChangedCallbackInfo* Data)
{
	FHCEos* Self = static_cast<FHCEos*>(Data->ClientData);
	if (Data->CurrentStatus != EOS_ELoginStatus::EOS_LS_LoggedIn && Data->LocalUserId == Self->LocalUserId && Self->State == EHCEosState::Ready)
	{
		Self->LoginFailed(TEXT("logged out by EOS"));
	}
}

// ---------------------------------------------------------------------------
// Lobby

void FHCEos::CreateLobby(const FString& Code, const FString& BuildVersion, int32 MaxMembers, TFunction<void(bool)> Done)
{
	if (State != EHCEosState::Ready)
	{
		Done(false);
		return;
	}
	if (HasLobby())
	{
		DestroyLobby();
	}
	EOS_Lobby_CreateLobbyOptions Options = {};
	Options.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
	Options.LocalUserId = LocalUserId;
	Options.MaxLobbyMembers = static_cast<uint32_t>(FMath::Clamp(MaxMembers, 2, 64));
	Options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
	Options.bPresenceEnabled = EOS_FALSE;
	Options.bAllowInvites = EOS_FALSE;
	Options.BucketId = "HC";
	Options.bDisableHostMigration = EOS_TRUE;
	Options.bEnableRTCRoom = EOS_FALSE; // voice runs through the game connection
	Options.bEnableJoinById = EOS_FALSE;
	EOS_Lobby_CreateLobby(Lobby, &Options, HCEosDetail::MakeCallback<EOS_Lobby_CreateLobbyCallbackInfo>([this, Code, BuildVersion, Done](const EOS_Lobby_CreateLobbyCallbackInfo* Data)
	{
		if (Data->ResultCode != EOS_EResult::EOS_Success || !Data->LobbyId)
		{
			UE_LOG(LogHCNet, Warning, TEXT("EOS lobby could not be created: %s"), *HCEosDetail::ResultText(Data->ResultCode));
			Done(false);
			return;
		}
		LobbyId = UTF8_TO_TCHAR(Data->LobbyId);
		TArray<TPair<FString, FString>> Attributes;
		Attributes.Emplace(UTF8_TO_TCHAR(HCEosDetail::KeyJoinCode), Code);
		Attributes.Emplace(UTF8_TO_TCHAR(HCEosDetail::KeyBuildVer), BuildVersion);
		Attributes.Emplace(UTF8_TO_TCHAR(HCEosDetail::KeyState), TEXT("LOBBY"));
		WriteLobbyAttributes(Attributes, [this, Code, Done](bool bOk)
		{
			if (bOk)
			{
				UE_LOG(LogHCNet, Log, TEXT("EOS lobby %s open with join code %s"), *LobbyId, *Code);
			}
			else
			{
				DestroyLobby();
			}
			Done(bOk);
		});
	}), &HCEosDetail::THCEosCallback<EOS_Lobby_CreateLobbyCallbackInfo>::Call);
}

void FHCEos::WriteLobbyAttributes(const TArray<TPair<FString, FString>>& Attributes, TFunction<void(bool)> Done)
{
	const FTCHARToUTF8 LobbyIdUtf8(*LobbyId);
	EOS_Lobby_UpdateLobbyModificationOptions ModOptions = {};
	ModOptions.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
	ModOptions.LocalUserId = LocalUserId;
	ModOptions.LobbyId = LobbyIdUtf8.Get();
	EOS_HLobbyModification Modification = nullptr;
	const EOS_EResult ModResult = EOS_Lobby_UpdateLobbyModification(Lobby, &ModOptions, &Modification);
	if (ModResult != EOS_EResult::EOS_Success || !Modification)
	{
		UE_LOG(LogHCNet, Warning, TEXT("EOS lobby update not possible: %s"), *HCEosDetail::ResultText(ModResult));
		Done(false);
		return;
	}
	for (const TPair<FString, FString>& Attribute : Attributes)
	{
		const FTCHARToUTF8 Key(*Attribute.Key);
		const FTCHARToUTF8 Value(*Attribute.Value);
		EOS_Lobby_AttributeData Data = {};
		Data.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
		Data.Key = Key.Get();
		Data.Value.AsUtf8 = Value.Get();
		Data.ValueType = EOS_EAttributeType::EOS_AT_STRING;
		EOS_LobbyModification_AddAttributeOptions AddOptions = {};
		AddOptions.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
		AddOptions.Attribute = &Data;
		AddOptions.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
		EOS_LobbyModification_AddAttribute(Modification, &AddOptions);
	}
	EOS_Lobby_UpdateLobbyOptions UpdateOptions = {};
	UpdateOptions.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
	UpdateOptions.LobbyModificationHandle = Modification;
	EOS_Lobby_UpdateLobby(Lobby, &UpdateOptions, HCEosDetail::MakeCallback<EOS_Lobby_UpdateLobbyCallbackInfo>([Done](const EOS_Lobby_UpdateLobbyCallbackInfo* Data)
	{
		if (Data->ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogHCNet, Warning, TEXT("EOS lobby update failed: %s"), *HCEosDetail::ResultText(Data->ResultCode));
		}
		Done(Data->ResultCode == EOS_EResult::EOS_Success);
	}), &HCEosDetail::THCEosCallback<EOS_Lobby_UpdateLobbyCallbackInfo>::Call);
	EOS_LobbyModification_Release(Modification);
}

void FHCEos::SetLobbyMatchRunning(bool bRunning)
{
	if (!HasLobby() || State != EHCEosState::Ready)
	{
		return;
	}
	TArray<TPair<FString, FString>> Attributes;
	Attributes.Emplace(UTF8_TO_TCHAR(HCEosDetail::KeyState), bRunning ? TEXT("MATCH") : TEXT("LOBBY"));
	WriteLobbyAttributes(Attributes, [](bool) {});
}

void FHCEos::DestroyLobby()
{
	if (!HasLobby())
	{
		return;
	}
	const FString Id = LobbyId;
	LobbyId.Reset();
	if (!Lobby || !LocalUserId)
	{
		return;
	}
	const FTCHARToUTF8 LobbyIdUtf8(*Id);
	EOS_Lobby_DestroyLobbyOptions Options = {};
	Options.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
	Options.LocalUserId = LocalUserId;
	Options.LobbyId = LobbyIdUtf8.Get();
	EOS_Lobby_DestroyLobby(Lobby, &Options, HCEosDetail::MakeCallback<EOS_Lobby_DestroyLobbyCallbackInfo>([Id](const EOS_Lobby_DestroyLobbyCallbackInfo* Data)
	{
		UE_LOG(LogHCNet, Log, TEXT("EOS lobby %s closed: %s"), *Id, *HCEosDetail::ResultText(Data->ResultCode));
	}), &HCEosDetail::THCEosCallback<EOS_Lobby_DestroyLobbyCallbackInfo>::Call);
}

void FHCEos::FindLobby(const FString& Code, TFunction<void(EHCEosFind, const FHCEosLobby&)> Done)
{
	if (State != EHCEosState::Ready)
	{
		Done(EHCEosFind::Failed, FHCEosLobby());
		return;
	}
	EOS_Lobby_CreateLobbySearchOptions SearchOptions = {};
	SearchOptions.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
	SearchOptions.MaxResults = 4;
	EOS_HLobbySearch Search = nullptr;
	const EOS_EResult CreateResult = EOS_Lobby_CreateLobbySearch(Lobby, &SearchOptions, &Search);
	if (CreateResult != EOS_EResult::EOS_Success || !Search)
	{
		UE_LOG(LogHCNet, Warning, TEXT("EOS search not possible: %s"), *HCEosDetail::ResultText(CreateResult));
		Done(EHCEosFind::Failed, FHCEosLobby());
		return;
	}
	const FTCHARToUTF8 CodeUtf8(*Code);
	EOS_Lobby_AttributeData Parameter = {};
	Parameter.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
	Parameter.Key = HCEosDetail::KeyJoinCode;
	Parameter.Value.AsUtf8 = CodeUtf8.Get();
	Parameter.ValueType = EOS_EAttributeType::EOS_AT_STRING;
	EOS_LobbySearch_SetParameterOptions ParameterOptions = {};
	ParameterOptions.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
	ParameterOptions.Parameter = &Parameter;
	ParameterOptions.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
	EOS_LobbySearch_SetParameter(Search, &ParameterOptions);

	EOS_LobbySearch_FindOptions FindOptions = {};
	FindOptions.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
	FindOptions.LocalUserId = LocalUserId;
	const EOS_ProductUserId Me = LocalUserId;
	EOS_LobbySearch_Find(Search, &FindOptions, HCEosDetail::MakeCallback<EOS_LobbySearch_FindCallbackInfo>([Search, Me, Code, Done](const EOS_LobbySearch_FindCallbackInfo* Data)
	{
		FHCEosLobby Found;
		EHCEosFind Result = EHCEosFind::NotFound;
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			EOS_LobbySearch_GetSearchResultCountOptions CountOptions = {};
			CountOptions.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
			const uint32_t Count = EOS_LobbySearch_GetSearchResultCount(Search, &CountOptions);
			for (uint32_t Index = 0; Index < Count && Result != EHCEosFind::Found; ++Index)
			{
				EOS_LobbySearch_CopySearchResultByIndexOptions CopyOptions = {};
				CopyOptions.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
				CopyOptions.LobbyIndex = Index;
				EOS_HLobbyDetails Details = nullptr;
				if (EOS_LobbySearch_CopySearchResultByIndex(Search, &CopyOptions, &Details) != EOS_EResult::EOS_Success || !Details)
				{
					continue;
				}
				EOS_LobbyDetails_GetLobbyOwnerOptions OwnerOptions = {};
				OwnerOptions.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
				const EOS_ProductUserId Owner = EOS_LobbyDetails_GetLobbyOwner(Details, &OwnerOptions);
				if (Owner && Owner != Me && EOS_ProductUserId_IsValid(Owner))
				{
					Found.HostId = LexToString(Owner);
					Found.BuildVersion = HCEosDetail::ReadStringAttribute(Details, HCEosDetail::KeyBuildVer);
					Found.bMatchRunning = HCEosDetail::ReadStringAttribute(Details, HCEosDetail::KeyState) == TEXT("MATCH");
					Result = EHCEosFind::Found;
				}
				else if (Owner == Me)
				{
					// A second game on the host's PC (same Windows user = same identity): it finds the host by LAN search
					UE_LOG(LogHCNet, Log, TEXT("Lobby %s belongs to this PC's own EOS identity (host on this PC): left to the LAN search"), *Code);
				}
				EOS_LobbyDetails_Release(Details);
			}
		}
		else if (Data->ResultCode != EOS_EResult::EOS_NotFound)
		{
			UE_LOG(LogHCNet, Warning, TEXT("EOS search failed: %s"), *HCEosDetail::ResultText(Data->ResultCode));
			Result = EHCEosFind::Failed;
		}
		EOS_LobbySearch_Release(Search);
		Done(Result, Found);
	}), &HCEosDetail::THCEosCallback<EOS_LobbySearch_FindCallbackInfo>::Call);
}

// ---------------------------------------------------------------------------
// Game tunnel over P2P

void FHCEos::BindP2P()
{
	if (!P2P || !LocalUserId || P2PBoundUser == LocalUserId)
	{
		return;
	}
	UnbindP2P();
	P2PBoundUser = LocalUserId;

	EOS_P2P_AddNotifyPeerConnectionRequestOptions RequestOptions = {};
	RequestOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
	RequestOptions.LocalUserId = LocalUserId;
	RequestOptions.SocketId = &TunnelSocket;
	RequestNotify = EOS_P2P_AddNotifyPeerConnectionRequest(P2P, &RequestOptions, this, &FHCEos::HandleConnectionRequest);

	EOS_P2P_AddNotifyPeerConnectionEstablishedOptions EstablishedOptions = {};
	EstablishedOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
	EstablishedOptions.LocalUserId = LocalUserId;
	EstablishedOptions.SocketId = &TunnelSocket;
	EstablishedNotify = EOS_P2P_AddNotifyPeerConnectionEstablished(P2P, &EstablishedOptions, this, &FHCEos::HandleConnectionEstablished);

	EOS_P2P_AddNotifyPeerConnectionInterruptedOptions InterruptedOptions = {};
	InterruptedOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST;
	InterruptedOptions.LocalUserId = LocalUserId;
	InterruptedOptions.SocketId = &TunnelSocket;
	InterruptedNotify = EOS_P2P_AddNotifyPeerConnectionInterrupted(P2P, &InterruptedOptions, this, &FHCEos::HandleConnectionInterrupted);

	EOS_P2P_AddNotifyPeerConnectionClosedOptions ClosedOptions = {};
	ClosedOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
	ClosedOptions.LocalUserId = LocalUserId;
	ClosedOptions.SocketId = &TunnelSocket;
	ClosedNotify = EOS_P2P_AddNotifyPeerConnectionClosed(P2P, &ClosedOptions, this, &FHCEos::HandleConnectionClosed);

	// For the log: how well this PC can be reached (open/moderate connect directly, strict mostly through a relay)
	EOS_P2P_QueryNATTypeOptions NatOptions = {};
	NatOptions.ApiVersion = EOS_P2P_QUERYNATTYPE_API_LATEST;
	EOS_P2P_QueryNATType(P2P, &NatOptions, nullptr, &FHCEos::HandleNatType);
}

void EOS_CALL FHCEos::HandleNatType(const EOS_P2P_OnQueryNATTypeCompleteInfo* Data)
{
	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogHCNet, Log, TEXT("EOS P2P: NAT type unknown (%s)"), *HCEosDetail::ResultText(Data->ResultCode));
		return;
	}
	const TCHAR* Type = Data->NATType == EOS_ENATType::EOS_NAT_Open ? TEXT("open")
		: Data->NATType == EOS_ENATType::EOS_NAT_Moderate ? TEXT("moderate")
		: Data->NATType == EOS_ENATType::EOS_NAT_Strict ? TEXT("strict (online players mostly through an Epic relay)") : TEXT("unknown");
	UE_LOG(LogHCNet, Log, TEXT("EOS P2P: NAT type of this network is %s"), Type);
}

void FHCEos::UnbindP2P()
{
	if (!P2P)
	{
		return;
	}
	if (RequestNotify != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_P2P_RemoveNotifyPeerConnectionRequest(P2P, RequestNotify);
	}
	if (EstablishedNotify != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_P2P_RemoveNotifyPeerConnectionEstablished(P2P, EstablishedNotify);
	}
	if (InterruptedNotify != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_P2P_RemoveNotifyPeerConnectionInterrupted(P2P, InterruptedNotify);
	}
	if (ClosedNotify != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_P2P_RemoveNotifyPeerConnectionClosed(P2P, ClosedNotify);
	}
	RequestNotify = EstablishedNotify = InterruptedNotify = ClosedNotify = EOS_INVALID_NOTIFICATIONID;
	P2PBoundUser = nullptr;
}

EOS_ProductUserId FHCEos::PeerUserId(const FString& PeerId)
{
	if (const EOS_ProductUserId* Known = PeerIds.Find(PeerId))
	{
		return *Known;
	}
	const EOS_ProductUserId Id = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*PeerId));
	if (!Id || EOS_ProductUserId_IsValid(Id) != EOS_TRUE)
	{
		return nullptr;
	}
	PeerIds.Add(PeerId, Id);
	return Id;
}

bool FHCEos::SendToPeer(const FString& PeerId, const uint8* Data, int32 Size)
{
	const EOS_ProductUserId Remote = PeerUserId(PeerId);
	if (!P2P || !LocalUserId || !Remote || Size <= 0 || Size > EOS_P2P_MAX_PACKET_SIZE)
	{
		return false;
	}
	EOS_P2P_SendPacketOptions Options = {};
	Options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	Options.LocalUserId = LocalUserId;
	Options.RemoteUserId = Remote;
	Options.SocketId = &TunnelSocket;
	Options.Channel = HCEosConfig::TunnelChannel;
	Options.DataLengthBytes = static_cast<uint32_t>(Size);
	Options.Data = Data;
	// Queued while the connection is still being set up (NAT traversal, relay): the first packets of the handshake
	Options.bAllowDelayedDelivery = EOS_TRUE;
	// The game's own reliability runs on top, exactly as over UDP
	Options.Reliability = EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
	Options.bDisableAutoAcceptConnection = EOS_FALSE;
	const EOS_EResult Result = EOS_P2P_SendPacket(P2P, &Options);
	if (Result != EOS_EResult::EOS_Success)
	{
		bool bAlreadyLogged = false;
		SendFailedPeers.Add(PeerId, &bAlreadyLogged);
		if (!bAlreadyLogged)
		{
			UE_LOG(LogHCNet, Warning, TEXT("EOS P2P: sending to ...%s failed: %s"), *PeerId.Right(8), *HCEosDetail::ResultText(Result));
		}
		return false;
	}
	return true;
}

void FHCEos::TickTunnels()
{
	if (!P2P || !LocalUserId)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	// Read everything that came in; without a tunnel the packets are only thrown away (a game that was left, an old join)
	uint8 Buffer[EOS_P2P_MAX_PACKET_SIZE];
	const uint8_t Channel = HCEosConfig::TunnelChannel;
	for (int32 i = 0; i < HCEosConfig::MaxPacketsPerTick; ++i)
	{
		EOS_P2P_ReceivePacketOptions Options = {};
		Options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
		Options.LocalUserId = LocalUserId;
		Options.MaxDataSizeBytes = EOS_P2P_MAX_PACKET_SIZE;
		Options.RequestedChannel = &Channel;
		EOS_ProductUserId Peer = nullptr;
		EOS_P2P_SocketId Socket = {};
		uint8_t OutChannel = 0;
		uint32_t Written = 0;
		const EOS_EResult Result = EOS_P2P_ReceivePacket(P2P, &Options, &Peer, &Socket, &OutChannel, Buffer, &Written);
		if (Result != EOS_EResult::EOS_Success)
		{
			break; // EOS_NotFound: nothing left
		}
		Socket.SocketName[EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1] = 0;
		if (!Peer || Written == 0 || FCStringAnsi::Strcmp(Socket.SocketName, HCEosConfig::TunnelSocketName) != 0)
		{
			continue;
		}
		const FString PeerId = LexToString(Peer);
		if (HostTunnel)
		{
			HostTunnel->FromPeer(PeerId, Buffer, static_cast<int32>(Written), Now);
		}
		else if (ClientTunnel)
		{
			ClientTunnel->FromPeer(PeerId, Buffer, static_cast<int32>(Written));
		}
	}
	if (HostTunnel)
	{
		HostTunnel->Tick(Now);
	}
	if (ClientTunnel)
	{
		ClientTunnel->Tick(Now);
	}
}

void FHCEos::ServeOnlinePlayers(int32 GamePort)
{
	if (!HostTunnel)
	{
		CloseTunnelToHost();
		HostTunnel = MakeUnique<FHCTunnelHost>(*this);
		SendFailedPeers.Reset();
		UE_LOG(LogHCNet, Log, TEXT("Online tunnel: the lobby takes online players through EOS P2P"));
	}
	HostTunnel->SetGamePort(GamePort);
}

void FHCEos::StopServingOnlinePlayers()
{
	if (!HostTunnel)
	{
		return;
	}
	HostTunnel.Reset();
	if (P2P && LocalUserId)
	{
		EOS_P2P_CloseConnectionsOptions Options = {};
		Options.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
		Options.LocalUserId = LocalUserId;
		Options.SocketId = &TunnelSocket;
		EOS_P2P_CloseConnections(P2P, &Options);
	}
	UE_LOG(LogHCNet, Log, TEXT("Online tunnel: lobby closed for online players"));
}

FString FHCEos::OpenTunnelToHost(const FString& HostId)
{
	CloseTunnelToHost();
	StopServingOnlinePlayers();
	if (State != EHCEosState::Ready || !P2P || !PeerUserId(HostId))
	{
		return FString();
	}
	ClientTunnel = MakeUnique<FHCTunnelClient>(*this, HostId);
	SendFailedPeers.Reset();
	if (!ClientTunnel->Start())
	{
		ClientTunnel.Reset();
		return FString();
	}
	return ClientTunnel->GetLocalAddress();
}

void FHCEos::CloseTunnelToHost()
{
	if (!ClientTunnel)
	{
		return;
	}
	const FString HostId = ClientTunnel->GetHostId();
	ClientTunnel.Reset();
	ClosePeerConnection(HostId);
	UE_LOG(LogHCNet, Log, TEXT("Online tunnel to the host closed"));
}

void FHCEos::ClosePeerConnection(const FString& PeerId)
{
	const EOS_ProductUserId Remote = PeerUserId(PeerId);
	if (!P2P || !LocalUserId || !Remote)
	{
		return;
	}
	EOS_P2P_CloseConnectionOptions Options = {};
	Options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
	Options.LocalUserId = LocalUserId;
	Options.RemoteUserId = Remote;
	Options.SocketId = &TunnelSocket;
	EOS_P2P_CloseConnection(P2P, &Options);
}

void EOS_CALL FHCEos::HandleConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* Data)
{
	FHCEos* Self = static_cast<FHCEos*>(Data->ClientData);
	const FString Peer = LexToString(Data->RemoteUserId);
	// Host: anyone who found the lobby by its code. Client: only the host it joined.
	const bool bWanted = Self->HostTunnel.IsValid() || (Self->ClientTunnel && Self->ClientTunnel->GetHostId() == Peer);
	if (!bWanted)
	{
		UE_LOG(LogHCNet, Log, TEXT("EOS P2P: connection request from ...%s ignored (no lobby open)"), *Peer.Right(8));
		return;
	}
	EOS_P2P_AcceptConnectionOptions Options = {};
	Options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
	Options.LocalUserId = Data->LocalUserId;
	Options.RemoteUserId = Data->RemoteUserId;
	Options.SocketId = &Self->TunnelSocket;
	const EOS_EResult Result = EOS_P2P_AcceptConnection(Self->P2P, &Options);
	UE_LOG(LogHCNet, Log, TEXT("EOS P2P: connection request from ...%s accepted: %s"), *Peer.Right(8), *HCEosDetail::ResultText(Result));
}

void EOS_CALL FHCEos::HandleConnectionEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* Data)
{
	const FString Peer = LexToString(Data->RemoteUserId);
	const TCHAR* Path = Data->NetworkType == EOS_ENetworkConnectionType::EOS_NCT_DirectConnection ? TEXT("direct")
		: Data->NetworkType == EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection ? TEXT("through an Epic relay") : TEXT("no path yet");
	UE_LOG(LogHCNet, Log, TEXT("EOS P2P: %s with ...%s, %s"),
		Data->ConnectionType == EOS_EConnectionEstablishedType::EOS_CET_Reconnection ? TEXT("reconnected") : TEXT("connected"), *Peer.Right(8), Path);
}

void EOS_CALL FHCEos::HandleConnectionInterrupted(const EOS_P2P_OnPeerConnectionInterruptedInfo* Data)
{
	UE_LOG(LogHCNet, Warning, TEXT("EOS P2P: connection with ...%s interrupted, EOS tries to restore it"), *LexToString(Data->RemoteUserId).Right(8));
}

void EOS_CALL FHCEos::HandleConnectionClosed(const EOS_P2P_OnRemoteConnectionClosedInfo* Data)
{
	// The tunnel keeps its sockets: a reconnect of the same join goes on with the same address; idle ones go after a while
	UE_LOG(LogHCNet, Log, TEXT("EOS P2P: connection with ...%s closed (reason %d)"), *LexToString(Data->RemoteUserId).Right(8), static_cast<int32>(Data->Reason));
}

#else // HC_WITH_EOS

/** Builds without EOS (Linux, see HCEos.h): LAN and IP only. */
class FHCEosOff : public IHCEos
{
public:
	virtual void Start(const FString&) override {}
	virtual EHCEosState GetState() const override { return EHCEosState::Off; }
	virtual FString GetError() const override { return FString(); }
	virtual void CreateLobby(const FString&, const FString&, int32, TFunction<void(bool)> Done) override { Done(false); }
	virtual void SetLobbyMatchRunning(bool) override {}
	virtual void DestroyLobby() override {}
	virtual bool HasLobby() const override { return false; }
	virtual void FindLobby(const FString&, TFunction<void(EHCEosFind, const FHCEosLobby&)> Done) override { Done(EHCEosFind::Failed, FHCEosLobby()); }
	virtual void ServeOnlinePlayers(int32) override {}
	virtual void StopServingOnlinePlayers() override {}
	virtual FString OpenTunnelToHost(const FString&) override { return FString(); }
	virtual void CloseTunnelToHost() override {}
};

IHCEos& IHCEos::Get()
{
	static FHCEosOff Instance;
	return Instance;
}

#endif // HC_WITH_EOS
