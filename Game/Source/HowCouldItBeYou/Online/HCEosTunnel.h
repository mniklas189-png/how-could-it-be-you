#pragma once

#include "CoreMinimal.h"

class FSocket;
class FInternetAddr;

/**
 * Online games through EOS P2P without Epic's net driver (docs/03 §7, since 0.13.12).
 *
 * The game connection always runs over the plain IpNetDriver. A player who joins over the internet gets a tunnel that
 * carries the game's UDP packets through EOS P2P (NAT traversal, Epic's relays when no direct path exists):
 *
 *   client game --UDP--> 127.0.0.1:<client tunnel> ==EOS P2P==> host tunnel --UDP--> 127.0.0.1:<game port> (listen server)
 *
 * So an online host still listens over IP and takes LAN, IP address (playit.gg), same-PC and internet players at once.
 * From 0.12.0 to 0.13.11 an online host listened over NetDriverEOS only: every LAN, IP and same-PC join failed, and
 * Linux players (no EOS) could not join a Windows host at all.
 *
 * Each P2P packet = 5-byte header (version, session id of the client tunnel) + one UDP datagram of the game
 * (the engine's MAX_PACKET_SIZE is 1024 bytes, an EOS packet takes 1170). A new session id (a new join of the same
 * player) gets a new local socket on the host, so the listen server sees a new address and a fresh handshake.
 *
 * This file knows nothing about EOS (IHCPeerLink): HCEos.cpp implements the link, the tests use an in-memory one.
 */
namespace HCTunnel
{
	constexpr uint8 Version = 1;
	constexpr int32 HeaderBytes = 5;
	/** EOS_P2P_MAX_PACKET_SIZE */
	constexpr int32 MaxPeerPacketBytes = 1170;
	constexpr int32 MaxGamePacketBytes = MaxPeerPacketBytes - HeaderBytes;
	/** Host: a peer that sent nothing for this long is forgotten (its game connection timed out long before). */
	constexpr double PeerIdleSeconds = 120.0;
	/** Host: at most this many online players at once (EOS_P2P_MAX_CONNECTIONS). */
	constexpr int32 MaxPeers = 32;

	void WriteHeader(uint8* Out, uint32 Session);
	/** False if the packet is too short or has another version. */
	bool ReadHeader(const uint8* Data, int32 Size, uint32& OutSession);
}

/** Sends packets to other players: EOS P2P in the game (HCEos.cpp), a queue in the tests. */
class IHCPeerLink
{
public:
	virtual ~IHCPeerLink() = default;
	virtual bool SendToPeer(const FString& PeerId, const uint8* Data, int32 Size) = 0;
};

/** Host side: one local UDP socket per online player, talking to the listen server's port. */
class FHCTunnelHost
{
public:
	explicit FHCTunnelHost(IHCPeerLink& InLink) : Link(InLink) {}
	~FHCTunnelHost();

	/** The listen server's port (0 = not listening yet: packets are dropped, the client's handshake repeats). */
	void SetGamePort(int32 Port);
	int32 GetGamePort() const { return GamePort; }

	/** A packet from an online player. */
	void FromPeer(const FString& PeerId, const uint8* Data, int32 Size, double Now);
	/** Sends what the listen server answered to its players, forgets idle ones. Every frame. */
	void Tick(double Now);

	int32 NumPeers() const { return Peers.Num(); }
	/** Local port the listen server sees for this player (0 = unknown), for tests and logs. */
	int32 GetPeerPort(const FString& PeerId) const;

private:
	struct FPeer
	{
		FSocket* Socket = nullptr;
		uint32 Session = 0;
		double LastHeard = 0.0;
		int32 LocalPort = 0;
	};
	bool OpenPeer(const FString& PeerId, FPeer& Peer, uint32 Session, double Now);
	static void ClosePeer(FPeer& Peer);

	IHCPeerLink& Link;
	TMap<FString, FPeer> Peers;
	int32 GamePort = 0;
	TSharedPtr<FInternetAddr> GameAddr;
};

/** Client side: the local address the game connects to instead of the host. */
class FHCTunnelClient
{
public:
	FHCTunnelClient(IHCPeerLink& InLink, const FString& InHostId);
	~FHCTunnelClient();

	/** Opens 127.0.0.1:<free port>; false if no socket could be opened. */
	bool Start();
	/** "127.0.0.1:<port>" for ClientTravel. */
	FString GetLocalAddress() const;
	int32 GetLocalPort() const { return LocalPort; }
	const FString& GetHostId() const { return HostId; }

	/** A packet from a peer; only the host's are passed on. */
	void FromPeer(const FString& PeerId, const uint8* Data, int32 Size);
	/** Sends what the game sent to the host. Every frame. */
	void Tick(double Now);

private:
	IHCPeerLink& Link;
	FString HostId;
	uint32 Session = 0;
	FSocket* Socket = nullptr;
	int32 LocalPort = 0;
	/** Where the game's net driver sends from; the host's packets go back there. */
	TSharedPtr<FInternetAddr> GameAddr;
};
