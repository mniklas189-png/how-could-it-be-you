#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"
#include "IPAddress.h"
#include "Online/HCEosTunnel.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "SocketTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HCTunnelTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter;

	/** Stands in for EOS P2P: keeps what a tunnel sends until the test hands it to the other side. */
	struct FQueueLink : public IHCPeerLink
	{
		struct FPacket
		{
			FString To;
			TArray<uint8> Data;
		};
		TArray<FPacket> Sent;

		virtual bool SendToPeer(const FString& PeerId, const uint8* Data, int32 Size) override
		{
			FPacket& Packet = Sent.AddDefaulted_GetRef();
			Packet.To = PeerId;
			Packet.Data.Append(Data, Size);
			return true;
		}
	};

	/** A UDP socket on 127.0.0.1 with a free port: the game's listen server or the game's client connection. */
	struct FGameSocket
	{
		FSocket* Socket = nullptr;
		int32 Port = 0;

		bool Open()
		{
			ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			const TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
			Addr->SetLoopbackAddress();
			Addr->SetPort(0);
			Socket = Subsystem->CreateSocket(NAME_DGram, TEXT("HC tunnel test"), FNetworkProtocolTypes::IPv4);
			if (!Socket || !Socket->SetNonBlocking(true) || !Socket->Bind(*Addr))
			{
				return false;
			}
			Port = Socket->GetPortNo();
			return true;
		}

		~FGameSocket()
		{
			if (Socket)
			{
				Socket->Close();
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
			}
		}

		bool SendText(const FString& Text, int32 ToPort)
		{
			const TSharedRef<FInternetAddr> To = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
			To->SetLoopbackAddress();
			To->SetPort(ToPort);
			const FTCHARToUTF8 Utf8(*Text);
			int32 Sent = 0;
			return Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *To);
		}

		/** Next text that arrived; OutFromPort = sender's port. */
		bool ReadText(FString& OutText, int32& OutFromPort)
		{
			uint32 Pending = 0;
			if (!Socket->HasPendingData(Pending))
			{
				return false;
			}
			const TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
			uint8 Buffer[2048];
			int32 Read = 0;
			if (!Socket->RecvFrom(Buffer, sizeof(Buffer) - 1, Read, *From) || Read <= 0)
			{
				return false;
			}
			Buffer[Read] = 0;
			OutText = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer));
			OutFromPort = From->GetPort();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTunnelHeaderTest, "HowCouldItBeYou.Online.TunnelHeader", HCTunnelTests::Flags)
bool FHCTunnelHeaderTest::RunTest(const FString& Parameters)
{
	uint8 Packet[HCTunnel::HeaderBytes + 1] = {};
	HCTunnel::WriteHeader(Packet, 0xA1B2C3D4u);
	Packet[HCTunnel::HeaderBytes] = 42;
	uint32 Session = 0;
	TestTrue(TEXT("Header round trip"), HCTunnel::ReadHeader(Packet, sizeof(Packet), Session));
	TestEqual(TEXT("Session id"), Session, 0xA1B2C3D4u);
	TestFalse(TEXT("A header without game data"), HCTunnel::ReadHeader(Packet, HCTunnel::HeaderBytes, Session));
	Packet[0] = HCTunnel::Version + 1;
	TestFalse(TEXT("Another version"), HCTunnel::ReadHeader(Packet, sizeof(Packet), Session));
	HCTunnel::WriteHeader(Packet, 0);
	TestFalse(TEXT("Session 0 is never used"), HCTunnel::ReadHeader(Packet, sizeof(Packet), Session));
	TestTrue(TEXT("A full game packet fits into an EOS packet"), 1024 <= HCTunnel::MaxGamePacketBytes);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCTunnelForwardTest, "HowCouldItBeYou.Online.TunnelForwarding", HCTunnelTests::Flags)
bool FHCTunnelForwardTest::RunTest(const FString& Parameters)
{
	using namespace HCTunnelTests;
	const FString HostId = TEXT("host-puid");
	const FString PlayerId = TEXT("player-puid");

	FGameSocket Server; // the host's listen server
	FGameSocket Game;   // the joining player's game connection
	if (!TestTrue(TEXT("Listen server socket"), Server.Open()) || !TestTrue(TEXT("Game socket"), Game.Open()))
	{
		return false;
	}

	FQueueLink HostLink;
	FQueueLink ClientLink;
	FHCTunnelHost Host(HostLink);
	Host.SetGamePort(Server.Port);
	TUniquePtr<FHCTunnelClient> Client = MakeUnique<FHCTunnelClient>(ClientLink, HostId);
	if (!TestTrue(TEXT("Client tunnel opens a local port"), Client->Start()))
	{
		return false;
	}
	TestEqual(TEXT("Travel address"), Client->GetLocalAddress(), FString::Printf(TEXT("127.0.0.1:%d"), Client->GetLocalPort()));

	// One step of both tunnels plus the "internet" in between
	auto Pump = [&]()
	{
		const double Now = FPlatformTime::Seconds();
		Client->Tick(Now);
		for (const FQueueLink::FPacket& Packet : ClientLink.Sent)
		{
			if (Packet.To == HostId)
			{
				Host.FromPeer(PlayerId, Packet.Data.GetData(), Packet.Data.Num(), Now);
			}
		}
		ClientLink.Sent.Reset();
		Host.Tick(Now);
		for (const FQueueLink::FPacket& Packet : HostLink.Sent)
		{
			if (Packet.To == PlayerId)
			{
				Client->FromPeer(HostId, Packet.Data.GetData(), Packet.Data.Num());
			}
		}
		HostLink.Sent.Reset();
	};
	auto WaitFor = [&](FGameSocket& Socket, FString& OutText, int32& OutPort) -> bool
	{
		const double End = FPlatformTime::Seconds() + 2.0;
		while (FPlatformTime::Seconds() < End)
		{
			Pump();
			if (Socket.ReadText(OutText, OutPort))
			{
				return true;
			}
			FPlatformProcess::Sleep(0.005f);
		}
		return false;
	};

	// Player -> host
	FString Text;
	int32 FromPort = 0;
	TestTrue(TEXT("Game sends to the tunnel"), Game.SendText(TEXT("hello host"), Client->GetLocalPort()));
	TestTrue(TEXT("The listen server gets the player's packet"), WaitFor(Server, Text, FromPort));
	TestEqual(TEXT("Packet arrives unchanged"), Text, FString(TEXT("hello host")));
	TestEqual(TEXT("One online player"), Host.NumPeers(), 1);
	const int32 PlayerPort = FromPort;
	TestEqual(TEXT("The listen server sees the player at the tunnel's local port"), PlayerPort, Host.GetPeerPort(PlayerId));

	// Host -> player (the listen server answers the address it saw)
	TestTrue(TEXT("Listen server answers"), Server.SendText(TEXT("welcome"), PlayerPort));
	TestTrue(TEXT("The game gets the answer"), WaitFor(Game, Text, FromPort));
	TestEqual(TEXT("Answer arrives unchanged"), Text, FString(TEXT("welcome")));
	TestEqual(TEXT("It comes from the tunnel's port, where the game connected to"), FromPort, Client->GetLocalPort());

	// A big packet (the engine's maximum is 1024 bytes)
	const FString Big = FString::ChrN(1024, TEXT('x'));
	TestTrue(TEXT("Big packet sent"), Game.SendText(Big, Client->GetLocalPort()));
	TestTrue(TEXT("Big packet arrives"), WaitFor(Server, Text, FromPort) && Text == Big);

	// Packets of somebody else or of an old join do not reach the game
	uint8 Foreign[HCTunnel::HeaderBytes + 3] = {};
	HCTunnel::WriteHeader(Foreign, 77);
	Client->FromPeer(TEXT("stranger"), Foreign, sizeof(Foreign));
	Client->FromPeer(HostId, Foreign, sizeof(Foreign));
	FPlatformProcess::Sleep(0.05f);
	TestFalse(TEXT("Foreign packets are dropped"), Game.ReadText(Text, FromPort));

	// The same player joins again (new client tunnel = new session): the listen server sees a new address
	Client = MakeUnique<FHCTunnelClient>(ClientLink, HostId);
	TestTrue(TEXT("Second client tunnel"), Client->Start());
	TestTrue(TEXT("Game sends again"), Game.SendText(TEXT("hello again"), Client->GetLocalPort()));
	TestTrue(TEXT("The listen server gets the new join"), WaitFor(Server, Text, FromPort));
	TestEqual(TEXT("New join's packet"), Text, FString(TEXT("hello again")));
	TestNotEqual(TEXT("A rejoin gets a new address on the host"), FromPort, PlayerPort);
	TestEqual(TEXT("Still one online player"), Host.NumPeers(), 1);
	return true;
}

#endif
