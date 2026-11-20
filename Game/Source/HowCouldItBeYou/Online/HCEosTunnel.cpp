#include "Online/HCEosTunnel.h"

#include "Core/HCLog.h"
#include "IPAddress.h"
#include "Misc/Guid.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "SocketTypes.h"

/** Named namespace: unity builds put several .cpp files into one translation unit. */
namespace HCTunnelDetail
{
	/** Upper bound of datagrams read per socket and frame, so a flood cannot stall a frame. */
	constexpr int32 MaxReadsPerTick = 256;
	/** A frame of a full lobby is a few KB; a bigger buffer only matters while a map loads. */
	constexpr int32 SocketBufferBytes = 256 * 1024;

	ISocketSubsystem* GetSockets()
	{
		return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	}

	/** UDP socket on 127.0.0.1 with a free port, non-blocking. */
	FSocket* OpenLoopbackSocket(const TCHAR* Description, int32& OutPort)
	{
		OutPort = 0;
		ISocketSubsystem* Subsystem = GetSockets();
		if (!Subsystem)
		{
			return nullptr;
		}
		const TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		Addr->SetLoopbackAddress();
		Addr->SetPort(0);
		FSocket* Socket = Subsystem->CreateSocket(NAME_DGram, Description, FNetworkProtocolTypes::IPv4);
		if (!Socket)
		{
			return nullptr;
		}
		int32 Size = 0;
		Socket->SetReceiveBufferSize(SocketBufferBytes, Size);
		Socket->SetSendBufferSize(SocketBufferBytes, Size);
		if (!Socket->SetNonBlocking(true) || !Socket->Bind(*Addr))
		{
			UE_LOG(LogHCNet, Warning, TEXT("Online tunnel: cannot open a UDP socket on 127.0.0.1"));
			Subsystem->DestroySocket(Socket);
			return nullptr;
		}
		OutPort = Socket->GetPortNo();
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

	/** Short form of a ProductUserId for the log. */
	FString ShortId(const FString& PeerId)
	{
		return PeerId.Len() > 8 ? PeerId.Right(8) : PeerId;
	}

	/**
	 * Reads one datagram into Buffer. False once nothing is waiting; OutRead = 0 if the read failed
	 * (on Windows a UDP socket reports an ICMP "port unreachable" as a failed read).
	 */
	bool ReadDatagram(FSocket* Socket, uint8* Buffer, int32 BufferSize, int32& OutRead, FInternetAddr& OutFrom)
	{
		uint32 Pending = 0;
		if (!Socket->HasPendingData(Pending))
		{
			return false;
		}
		OutRead = 0;
		if (!Socket->RecvFrom(Buffer, BufferSize, OutRead, OutFrom) || OutRead <= 0)
		{
			OutRead = 0;
		}
		return true;
	}
}

namespace HCTunnel
{
	void WriteHeader(uint8* Out, uint32 Session)
	{
		Out[0] = Version;
		Out[1] = static_cast<uint8>(Session >> 24);
		Out[2] = static_cast<uint8>(Session >> 16);
		Out[3] = static_cast<uint8>(Session >> 8);
		Out[4] = static_cast<uint8>(Session);
	}

	bool ReadHeader(const uint8* Data, int32 Size, uint32& OutSession)
	{
		if (!Data || Size <= HeaderBytes || Data[0] != Version)
		{
			return false;
		}
		OutSession = (static_cast<uint32>(Data[1]) << 24) | (static_cast<uint32>(Data[2]) << 16)
			| (static_cast<uint32>(Data[3]) << 8) | static_cast<uint32>(Data[4]);
		return OutSession != 0;
	}
}

// ---------------------------------------------------------------------------
// Host

FHCTunnelHost::~FHCTunnelHost()
{
	for (TPair<FString, FPeer>& Pair : Peers)
	{
		ClosePeer(Pair.Value);
	}
	Peers.Reset();
}

void FHCTunnelHost::SetGamePort(int32 Port)
{
	if (Port == GamePort)
	{
		return;
	}
	GamePort = Port;
	GameAddr.Reset();
	ISocketSubsystem* Subsystem = HCTunnelDetail::GetSockets();
	if (Port > 0 && Subsystem)
	{
		GameAddr = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		GameAddr->SetLoopbackAddress();
		GameAddr->SetPort(Port);
		UE_LOG(LogHCNet, Log, TEXT("Online tunnel: online players reach the game at 127.0.0.1:%d"), Port);
	}
}

int32 FHCTunnelHost::GetPeerPort(const FString& PeerId) const
{
	const FPeer* Peer = Peers.Find(PeerId);
	return Peer ? Peer->LocalPort : 0;
}

bool FHCTunnelHost::OpenPeer(const FString& PeerId, FPeer& Peer, uint32 Session, double Now)
{
	ClosePeer(Peer);
	Peer.Socket = HCTunnelDetail::OpenLoopbackSocket(TEXT("HC online player"), Peer.LocalPort);
	Peer.Session = Session;
	Peer.LastHeard = Now;
	if (Peer.Socket)
	{
		UE_LOG(LogHCNet, Log, TEXT("Online tunnel: player ...%s connects through EOS P2P (seen by the game as 127.0.0.1:%d)"),
			*HCTunnelDetail::ShortId(PeerId), Peer.LocalPort);
	}
	return Peer.Socket != nullptr;
}

void FHCTunnelHost::ClosePeer(FPeer& Peer)
{
	HCTunnelDetail::CloseSocket(Peer.Socket);
	Peer.LocalPort = 0;
}

void FHCTunnelHost::FromPeer(const FString& PeerId, const uint8* Data, int32 Size, double Now)
{
	uint32 Session = 0;
	if (!HCTunnel::ReadHeader(Data, Size, Session))
	{
		return;
	}
	FPeer* Peer = Peers.Find(PeerId);
	if (!Peer)
	{
		if (Peers.Num() >= HCTunnel::MaxPeers)
		{
			return;
		}
		Peer = &Peers.Add(PeerId);
		if (!OpenPeer(PeerId, *Peer, Session, Now))
		{
			Peers.Remove(PeerId);
			return;
		}
	}
	else if (Peer->Session != Session)
	{
		// Joined again (new tunnel on the client): a new address, so the listen server starts a fresh handshake
		UE_LOG(LogHCNet, Log, TEXT("Online tunnel: player ...%s joined again"), *HCTunnelDetail::ShortId(PeerId));
		if (!OpenPeer(PeerId, *Peer, Session, Now))
		{
			Peers.Remove(PeerId);
			return;
		}
	}
	Peer->LastHeard = Now;
	if (!GameAddr.IsValid() || !Peer->Socket)
	{
		return;
	}
	int32 Sent = 0;
	Peer->Socket->SendTo(Data + HCTunnel::HeaderBytes, Size - HCTunnel::HeaderBytes, Sent, *GameAddr);
}

void FHCTunnelHost::Tick(double Now)
{
	ISocketSubsystem* Subsystem = HCTunnelDetail::GetSockets();
	if (!Subsystem)
	{
		return;
	}
	const TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	uint8 Buffer[HCTunnel::MaxPeerPacketBytes];
	TArray<FString> Idle;
	for (TPair<FString, FPeer>& Pair : Peers)
	{
		FPeer& Peer = Pair.Value;
		if (Now - Peer.LastHeard > HCTunnel::PeerIdleSeconds)
		{
			Idle.Add(Pair.Key);
			continue;
		}
		if (!Peer.Socket)
		{
			continue;
		}
		HCTunnel::WriteHeader(Buffer, Peer.Session);
		int32 Read = 0;
		for (int32 i = 0; i < HCTunnelDetail::MaxReadsPerTick; ++i)
		{
			if (!HCTunnelDetail::ReadDatagram(Peer.Socket, Buffer + HCTunnel::HeaderBytes, HCTunnel::MaxGamePacketBytes, Read, *From))
			{
				break;
			}
			// Only what the listen server sends goes out (the socket is on 127.0.0.1, so only this PC can reach it)
			if (Read > 0 && From->GetPort() == GamePort)
			{
				Link.SendToPeer(Pair.Key, Buffer, Read + HCTunnel::HeaderBytes);
			}
		}
	}
	for (const FString& PeerId : Idle)
	{
		UE_LOG(LogHCNet, Log, TEXT("Online tunnel: player ...%s gone"), *HCTunnelDetail::ShortId(PeerId));
		ClosePeer(Peers[PeerId]);
		Peers.Remove(PeerId);
	}
}

// ---------------------------------------------------------------------------
// Client

FHCTunnelClient::FHCTunnelClient(IHCPeerLink& InLink, const FString& InHostId)
	: Link(InLink)
	, HostId(InHostId)
{
	// Never 0 (ReadHeader rejects it); a new one per join, so the host knows a rejoin
	const FGuid Guid = FGuid::NewGuid();
	Session = Guid.A ^ Guid.B ^ Guid.C ^ Guid.D;
	if (Session == 0)
	{
		Session = 1;
	}
}

FHCTunnelClient::~FHCTunnelClient()
{
	HCTunnelDetail::CloseSocket(Socket);
}

bool FHCTunnelClient::Start()
{
	HCTunnelDetail::CloseSocket(Socket);
	GameAddr.Reset();
	Socket = HCTunnelDetail::OpenLoopbackSocket(TEXT("HC online tunnel"), LocalPort);
	if (Socket)
	{
		UE_LOG(LogHCNet, Log, TEXT("Online tunnel to host ...%s on 127.0.0.1:%d"), *HCTunnelDetail::ShortId(HostId), LocalPort);
	}
	return Socket != nullptr;
}

FString FHCTunnelClient::GetLocalAddress() const
{
	return Socket ? FString::Printf(TEXT("127.0.0.1:%d"), LocalPort) : FString();
}

void FHCTunnelClient::FromPeer(const FString& PeerId, const uint8* Data, int32 Size)
{
	uint32 PacketSession = 0;
	if (!Socket || !GameAddr.IsValid() || PeerId != HostId || !HCTunnel::ReadHeader(Data, Size, PacketSession) || PacketSession != Session)
	{
		return; // another peer, an old join, or the game has not sent anything yet
	}
	int32 Sent = 0;
	Socket->SendTo(Data + HCTunnel::HeaderBytes, Size - HCTunnel::HeaderBytes, Sent, *GameAddr);
}

void FHCTunnelClient::Tick(double Now)
{
	ISocketSubsystem* Subsystem = HCTunnelDetail::GetSockets();
	if (!Socket || !Subsystem)
	{
		return;
	}
	const TSharedRef<FInternetAddr> From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	uint8 Buffer[HCTunnel::MaxPeerPacketBytes];
	HCTunnel::WriteHeader(Buffer, Session);
	int32 Read = 0;
	for (int32 i = 0; i < HCTunnelDetail::MaxReadsPerTick; ++i)
	{
		if (!HCTunnelDetail::ReadDatagram(Socket, Buffer + HCTunnel::HeaderBytes, HCTunnel::MaxGamePacketBytes, Read, *From))
		{
			break;
		}
		if (Read <= 0)
		{
			continue;
		}
		// The game's net driver is the only sender (127.0.0.1); a new connect attempt may come from a new port
		if (!GameAddr.IsValid() || !(*GameAddr == *From))
		{
			GameAddr = From->Clone();
		}
		Link.SendToPeer(HostId, Buffer, Read + HCTunnel::HeaderBytes);
	}
}
