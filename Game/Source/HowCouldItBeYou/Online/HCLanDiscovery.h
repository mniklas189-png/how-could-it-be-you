#pragma once

#include "CoreMinimal.h"

class FSocket;

/**
 * Finding a lobby by its join code in the local network (docs/03 §7.2), without the engine's LAN beacon.
 *
 * OnlineSubsystemNull sends its search and the host's answer to 255.255.255.255. Windows sends such a packet out of
 * one adapter only, the one with the best metric, and on many PCs that is a virtual one (Hyper-V "Default Switch",
 * VirtualBox, VPN): the answer never reaches the home network and the other PC reports "no lobby with this code".
 *
 * Here the search goes out of every adapter (one socket bound to each adapter's address: Windows and Linux send a
 * 255.255.255.255 packet through the adapter the socket is bound to), and the host answers straight to the sender
 * (unicast, routed through the right adapter; Windows Firewall lets an answer to one's own broadcast in).
 */
namespace HCLanDiscovery
{
	/** UDP port hosts listen on (the engine's LAN beacon has 14001). */
	constexpr int32 Port = 14011;

	/** How long a client searches before "no lobby with this code". */
	constexpr double SearchSeconds = 4.0;

	struct FAnswer
	{
		FString Code;
		FString BuildVersion;
		bool bMatchRunning = false;
		int32 GamePort = 0;
	};

	// Packets: pure logic, tests in Tests/HCMatchLogicTests.cpp
	FString MakeQuery(const FString& Code);
	bool ParseQuery(const FString& Packet, FString& OutCode);
	FString MakeAnswer(const FAnswer& Answer);
	bool ParseAnswer(const FString& Packet, FAnswer& OutAnswer);

	/** Whether the search should also go out of an adapter with this address (IPv4, not loopback, not 169.254.x.x). */
	bool IsSearchAdapterAddress(const FString& Ip);
}

/** Host side: answers searches for the lobby's join code. */
class FHCLanResponder
{
public:
	~FHCLanResponder() { Stop(); }

	/** Listens on 0.0.0.0:HCLanDiscovery::Port. False if the port cannot be opened. */
	bool Start();
	void Stop();

	/** Answers every waiting search that asks for Answer.Code. */
	void Poll(const HCLanDiscovery::FAnswer& Answer);

private:
	FSocket* Socket = nullptr;
};

/** Client side: sends the search out of every adapter a few times and waits for the host's answer. */
class FHCLanSearch
{
public:
	~FHCLanSearch() { Stop(); }

	/** False if not a single socket could be opened. */
	bool Start(const FString& Code);
	void Stop();

	int32 NumSockets() const { return Sockets.Num(); }

	/** Sends again when due and reads answers. True once the host answered (OutHostIp = sender, e.g. "192.168.178.40"). */
	bool Poll(double Now, HCLanDiscovery::FAnswer& OutAnswer, FString& OutHostIp);

private:
	void SendQuery();

	/** [0] is bound to 0.0.0.0, the others each to one adapter's address. */
	TArray<FSocket*> Sockets;
	FString Code;
	double NextSend = 0.0;
};
