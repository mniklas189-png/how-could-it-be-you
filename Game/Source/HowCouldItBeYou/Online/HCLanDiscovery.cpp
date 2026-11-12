#include "Online/HCLanDiscovery.h"

#include "Core/HCLog.h"
#include "IPAddress.h"
#include "Online/HCJoinCode.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "SocketTypes.h"

namespace HCLanDiscovery
{
	namespace Detail
	{
		const TCHAR* const Magic = TEXT("HCLAN1");
		constexpr double ResendSeconds = 0.5;
		constexpr int32 MaxPacketBytes = 512;
		/** Upper bound of packets read per socket and poll, so a flood cannot stall a frame. */
		constexpr int32 MaxReadsPerPoll = 32;

		bool IsValidCode(const FString& Code)
		{
			FString Normalized;
			return FHCJoinCode::Normalize(Code, Normalized) && Normalized.Equals(Code, ESearchCase::CaseSensitive);
		}

		ISocketSubsystem* GetSockets()
		{
			return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		}

		/** UDP socket, non-blocking, broadcast allowed, bound to Ip:BindPort (empty Ip = every adapter). */
		FSocket* OpenSocket(const FString& Ip, int32 BindPort, bool bReuse, const TCHAR* Description)
		{
			ISocketSubsystem* Subsystem = GetSockets();
			if (!Subsystem)
			{
				return nullptr;
			}
			const FName IPv4 = FNetworkProtocolTypes::IPv4;
			const TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr(IPv4);
			if (Ip.IsEmpty())
			{
				Addr->SetAnyAddress();
			}
			else
			{
				bool bValid = false;
				Addr->SetIp(*Ip, bValid);
				if (!bValid)
				{
					return nullptr;
				}
			}
			Addr->SetPort(BindPort);
			FSocket* Socket = Subsystem->CreateSocket(NAME_DGram, Description, IPv4);
			if (!Socket)
			{
				return nullptr;
			}
			if (bReuse)
			{
				Socket->SetReuseAddr(true);
			}
			if (!Socket->SetNonBlocking(true) || !Socket->SetBroadcast(true) || !Socket->Bind(*Addr))
			{
				UE_LOG(LogHCNet, Log, TEXT("LAN search: cannot open UDP %s"), *Addr->ToString(true));
				Subsystem->DestroySocket(Socket);
				return nullptr;
			}
			return Socket;
		}

		void CloseSocket(FSocket*& Socket)
		{
			if (Socket)
			{
				Socket->Close();
				if (ISocketSubsystem* Subsystem = GetSockets())
				{
					Subsystem->DestroySocket(Socket);
				}
				Socket = nullptr;
			}
		}

		bool SendText(FSocket* Socket, const FString& Text, const FInternetAddr& To)
		{
			const FTCHARToUTF8 Utf8(*Text);
			int32 Sent = 0;
			return Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, To) && Sent == Utf8.Length();
		}

		/** Reads one datagram. False once nothing is waiting; OutText is empty if the read failed (e.g. a Windows ICMP reset). */
		bool ReceiveText(FSocket* Socket, FString& OutText, FInternetAddr& OutFrom)
		{
			uint32 Pending = 0;
			if (!Socket->HasPendingData(Pending))
			{
				return false;
			}
			uint8 Buffer[MaxPacketBytes + 1];
			int32 Read = 0;
			if (!Socket->RecvFrom(Buffer, MaxPacketBytes, Read, OutFrom) || Read <= 0)
			{
				OutText.Reset();
				return true; // consumed an error or an empty packet; the caller reads on
			}
			Buffer[Read] = 0;
			OutText = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer));
			return true;
		}
	}

	FString MakeQuery(const FString& Code)
	{
		return FString::Printf(TEXT("%s|Q|%s"), Detail::Magic, *Code);
	}

	bool ParseQuery(const FString& Packet, FString& OutCode)
	{
		TArray<FString> Parts;
		Packet.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() != 3 || Parts[0] != Detail::Magic || Parts[1] != TEXT("Q") || !Detail::IsValidCode(Parts[2]))
		{
			return false;
		}
		OutCode = Parts[2];
		return true;
	}

	FString MakeAnswer(const FAnswer& Answer)
	{
		const FString Version = Answer.BuildVersion.Replace(TEXT("|"), TEXT("_"));
		return FString::Printf(TEXT("%s|A|%s|%s|%s|%d"), Detail::Magic, *Answer.Code, *Version,
			Answer.bMatchRunning ? TEXT("MATCH") : TEXT("LOBBY"), Answer.GamePort);
	}

	bool ParseAnswer(const FString& Packet, FAnswer& OutAnswer)
	{
		TArray<FString> Parts;
		Packet.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() != 6 || Parts[0] != Detail::Magic || Parts[1] != TEXT("A") || !Detail::IsValidCode(Parts[2]))
		{
			return false;
		}
		if (Parts[4] != TEXT("LOBBY") && Parts[4] != TEXT("MATCH"))
		{
			return false;
		}
		if (Parts[5].IsEmpty() || Parts[5].Len() > 5 || !Parts[5].IsNumeric())
		{
			return false;
		}
		const int32 GamePort = FCString::Atoi(*Parts[5]);
		if (GamePort <= 0 || GamePort > 65535)
		{
			return false;
		}
		OutAnswer.Code = Parts[2];
		OutAnswer.BuildVersion = Parts[3];
		OutAnswer.bMatchRunning = Parts[4] == TEXT("MATCH");
		OutAnswer.GamePort = GamePort;
		return true;
	}

	bool IsSearchAdapterAddress(const FString& Ip)
	{
		if (Ip.IsEmpty() || Ip.Contains(TEXT(":")) || Ip.StartsWith(TEXT("127.")) || Ip.StartsWith(TEXT("169.254.")) || Ip.StartsWith(TEXT("0.")))
		{
			return false;
		}
		TArray<FString> Parts;
		Ip.ParseIntoArray(Parts, TEXT("."), false);
		return Parts.Num() == 4;
	}
}

// ---------------------------------------------------------------------------
// Host

bool FHCLanResponder::Start()
{
	Stop();
	// Reuse: several games on one PC (tests) all get the broadcast searches
	Socket = HCLanDiscovery::Detail::OpenSocket(FString(), HCLanDiscovery::Port, true, TEXT("HC LAN search answers"));
	if (Socket)
	{
		UE_LOG(LogHCNet, Log, TEXT("LAN search: lobby answers searches on UDP port %d"), HCLanDiscovery::Port);
	}
	return Socket != nullptr;
}

void FHCLanResponder::Stop()
{
	HCLanDiscovery::Detail::CloseSocket(Socket);
}

void FHCLanResponder::Poll(const HCLanDiscovery::FAnswer& Answer)
{
	ISocketSubsystem* Subsystem = HCLanDiscovery::Detail::GetSockets();
	if (!Socket || !Subsystem)
	{
		return;
	}
	const TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	FString Text;
	for (int32 i = 0; i < HCLanDiscovery::Detail::MaxReadsPerPoll; ++i)
	{
		if (!HCLanDiscovery::Detail::ReceiveText(Socket, Text, *From))
		{
			break;
		}
		FString Code;
		if (!HCLanDiscovery::ParseQuery(Text, Code) || !Code.Equals(Answer.Code, ESearchCase::CaseSensitive))
		{
			continue;
		}
		const bool bSent = HCLanDiscovery::Detail::SendText(Socket, HCLanDiscovery::MakeAnswer(Answer), *From);
		UE_LOG(LogHCNet, Verbose, TEXT("LAN search for %s from %s %s"), *Code, *From->ToString(true), bSent ? TEXT("answered") : TEXT("NOT answered"));
	}
}

// ---------------------------------------------------------------------------
// Client

bool FHCLanSearch::Start(const FString& InCode)
{
	Stop();
	Code = InCode;
	ISocketSubsystem* Subsystem = HCLanDiscovery::Detail::GetSockets();
	if (!Subsystem)
	{
		return false;
	}
	if (FSocket* Any = HCLanDiscovery::Detail::OpenSocket(FString(), 0, false, TEXT("HC LAN search")))
	{
		Sockets.Add(Any);
	}
	TArray<TSharedPtr<FInternetAddr>> Adapters;
	TArray<FString> Bound;
	if (Subsystem->GetLocalAdapterAddresses(Adapters))
	{
		for (const TSharedPtr<FInternetAddr>& Addr : Adapters)
		{
			const FString Ip = Addr.IsValid() ? Addr->ToString(false) : FString();
			if (!HCLanDiscovery::IsSearchAdapterAddress(Ip) || Bound.Contains(Ip))
			{
				continue;
			}
			if (FSocket* Socket = HCLanDiscovery::Detail::OpenSocket(Ip, 0, false, TEXT("HC LAN search")))
			{
				Sockets.Add(Socket);
				Bound.Add(Ip);
			}
		}
	}
	UE_LOG(LogHCNet, Log, TEXT("LAN search for %s, sending from 0.0.0.0%s%s"), *Code, Bound.Num() ? TEXT(", ") : TEXT(""), *FString::Join(Bound, TEXT(", ")));
	NextSend = 0.0;
	return Sockets.Num() > 0;
}

void FHCLanSearch::Stop()
{
	for (FSocket*& Socket : Sockets)
	{
		HCLanDiscovery::Detail::CloseSocket(Socket);
	}
	Sockets.Reset();
}

void FHCLanSearch::SendQuery()
{
	ISocketSubsystem* Subsystem = HCLanDiscovery::Detail::GetSockets();
	if (!Subsystem)
	{
		return;
	}
	const FString Query = HCLanDiscovery::MakeQuery(Code);
	const TSharedRef<FInternetAddr> Broadcast = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	Broadcast->SetBroadcastAddress();
	Broadcast->SetPort(HCLanDiscovery::Port);
	for (FSocket* Socket : Sockets)
	{
		HCLanDiscovery::Detail::SendText(Socket, Query, *Broadcast);
	}
	// A host on this PC answers even without any network
	if (Sockets.Num() > 0)
	{
		const TSharedRef<FInternetAddr> Loopback = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		Loopback->SetLoopbackAddress();
		Loopback->SetPort(HCLanDiscovery::Port);
		HCLanDiscovery::Detail::SendText(Sockets[0], Query, *Loopback);
	}
}

bool FHCLanSearch::Poll(double Now, HCLanDiscovery::FAnswer& OutAnswer, FString& OutHostIp)
{
	ISocketSubsystem* Subsystem = HCLanDiscovery::Detail::GetSockets();
	if (!Subsystem || Sockets.Num() == 0)
	{
		return false;
	}
	if (Now >= NextSend)
	{
		SendQuery();
		NextSend = Now + HCLanDiscovery::Detail::ResendSeconds;
	}
	const TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	FString Text;
	for (FSocket* Socket : Sockets)
	{
		for (int32 i = 0; i < HCLanDiscovery::Detail::MaxReadsPerPoll; ++i)
		{
			if (!HCLanDiscovery::Detail::ReceiveText(Socket, Text, *From))
			{
				break;
			}
			HCLanDiscovery::FAnswer Answer;
			if (HCLanDiscovery::ParseAnswer(Text, Answer) && Answer.Code.Equals(Code, ESearchCase::CaseSensitive))
			{
				OutAnswer = Answer;
				OutHostIp = From->ToString(false);
				return true;
			}
		}
	}
	return false;
}
