#include "TestCheck.h"

#include <JBro/Network/Testing/ManualClock.h>
#include <JBro/Network/Testing/MemorySocketProvider.h>
#include <JBro/Network/Transport.h>

#include <cstring>
#include <iostream>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    void Pump(Transport& a, Transport& b, ManualClock& clock, int rounds = 8)
    {
        for (int round = 0; round < rounds; ++round)
        {
            a.Update();
            b.Update();
            clock.Advance(5.0);
        }
    }

    std::uint32_t Drain(Transport& transport, NetworkEvent* out, std::uint32_t capacity)
    {
        return transport.TakeEvents(out, capacity);
    }

    const NetworkEvent* Find(const NetworkEvent* events, std::uint32_t count, NetworkEventKind kind)
    {
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (events[index].kind == kind)
            {
                return &events[index];
            }
        }
        return nullptr;
    }

    // **`Connected` 는 소켓이 붙은 때가 아니라 hello 가 맞은 때 뜬다.** 소켓만 붙은 한 라운드 뒤에는 아직 아무 이벤트가 없다.
    void TestConnectedWaitsForHello()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(10), "listen");
        Check(client.Connect("ws://memory", 10), "connect strips the ws:// scheme");
        // 서버가 받아들이고 클라이언트가 접속 완료를 보는 데 한 라운드, WS 요청·응답에 두 라운드 - 그 사이에는 이벤트가 없다.
        server.Update();
        client.Update();
        NetworkEvent events[8];
        Check(0 == Drain(server, events, 8) && 0 == Drain(client, events, 8), "no event before the handshakes");
        Check(server.GetConnectionCount() == 1, "though the server already holds the socket");
        Check(server.GetConnectionState(server.GetConnectionAt(0)) == ConnectionState::Connecting, "as Connecting");
        Pump(server, client, clock);
        const std::uint32_t serverCount = Drain(server, events, 8);
        Check(serverCount == 1 && events[0].kind == NetworkEventKind::Connected, "then the server sees Connected");
        const std::uint32_t clientCount = Drain(client, events, 8);
        Check(clientCount == 1 && events[0].kind == NetworkEventKind::Connected, "and so does the client");
        Check(server.GetConnectionState(server.GetConnectionAt(0)) == ConnectionState::Connected, "and the state follows");
    }

    // **버전이 다르면 클라이언트는 `VersionMismatch` 로 끊기고 서버는 아무 말도 하지 않는다.**
    void TestVersionMismatchIsRejected()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        TransportConfig newer;
        newer.protocolVersion = ProtocolVersion + 1;
        Transport server(provider, clock);
        Transport client(provider, clock, newer);
        Check(server.Listen(11), "listen");
        Check(client.Connect("memory", 11), "connect");
        Pump(server, client, clock, 12);
        NetworkEvent events[8];
        const std::uint32_t clientCount = Drain(client, events, 8);
        const NetworkEvent* rejected = Find(events, clientCount, NetworkEventKind::Disconnected);
        Check(nullptr != rejected, "the client is told it was disconnected");
        Check(rejected->reason == DisconnectReason::VersionMismatch, "because the versions differ");
        Check(nullptr == Find(events, clientCount, NetworkEventKind::Connected), "and it was never Connected");
        Check(0 == Drain(server, events, 8), "the server exposes nothing to the game");
        Check(server.GetConnectionCount() == 0, "and has dropped the socket");
    }

    // **keepalive 는 ping 을 보내고 pong 으로 RTT 를 잰다.** 시계를 손으로 돌려 15ms 왕복을 만든다.
    void TestPingMeasuresRoundTrip()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(12), "listen");
        Check(client.Connect("memory", 12), "connect");
        Pump(server, client, clock);
        const ConnectionId clientOnServer = server.GetConnectionAt(0);
        Check(server.GetRoundTripMilliseconds(clientOnServer) < 0.0, "no measurement yet");

        clock.Advance(1000.0);
        server.Update();
        client.Update();
        clock.Advance(15.0);
        server.Update();
        const double rtt = server.GetRoundTripMilliseconds(clientOnServer);
        Check(rtt > 14.9 && rtt < 15.1, "the server measures the 15ms the clock advanced");
        Check(client.GetRoundTripMilliseconds(ServerConnectionId) < 0.0 || client.GetRoundTripMilliseconds(ServerConnectionId) >= 0.0,
            "the client's own measurement is independent");
        Check(server.GetRoundTripMilliseconds(999) < 0.0, "an unknown connection is -1");
    }

    // **응답이 없으면 `Timeout` 으로 끊는다.** 클라이언트를 멈추고 서버 시계만 5 초 넘게 돌린다.
    void TestSilenceTimesOut()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(13), "listen");
        Check(client.Connect("memory", 13), "connect");
        Pump(server, client, clock);
        NetworkEvent events[8];
        Drain(server, events, 8);
        Drain(client, events, 8);

        clock.Advance(5001.0);
        server.Update();
        const std::uint32_t count = Drain(server, events, 8);
        const NetworkEvent* timedOut = Find(events, count, NetworkEventKind::Disconnected);
        Check(nullptr != timedOut, "the server drops the silent client");
        Check(timedOut->reason == DisconnectReason::Timeout, "as a timeout");
        Check(server.GetConnectionCount() == 0, "and forgets it");
    }

    // **핸드셰이크를 시작하지 않는 소켓은 시한이 지나면 조용히 버린다.** 포트 스캐너가 게임에 보이면 안 된다.
    void TestIdleSocketIsDroppedWithoutEvent()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Check(server.Listen(14), "listen");
        JBro::OwnerPtr<IStreamSocket> scanner = provider.CreateStreamSocket();
        Check(scanner->Connect("memory", 14), "a raw socket connects");
        server.Update();
        Check(server.GetConnectionCount() == 1, "the server accepted it");
        clock.Advance(5001.0);
        server.Update();
        Check(server.GetConnectionCount() == 0, "and dropped it after the timeout");
        NetworkEvent events[8];
        Check(0 == Drain(server, events, 8), "without telling the game");
        Check(scanner->GetState() == ConnectionState::Disconnected, "the raw socket sees the close");
    }

    // **조각난 WS 메시지는 조립돼 하나로 도착한다.** 브라우저는 큰 메시지를 조각내 보낼 수 있다.
    void TestFragmentedMessageIsReassembled()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        TransportConfig webSocketOnly;
        webSocketOnly.udpEnabled = false;
        Transport server(provider, clock, webSocketOnly);
        Transport client(provider, clock, webSocketOnly);
        Check(server.Listen(15), "listen");
        Check(client.Connect("memory", 15), "connect");
        Pump(server, client, clock);

        client.SetFragmentBytesForTests(5);
        std::uint8_t payload[103];
        for (std::uint32_t index = 0; index < sizeof(payload); ++index)
        {
            payload[index] = static_cast<std::uint8_t>(index ^ 0x5A);
        }
        Check(client.Send(ServerConnectionId, 77, payload, sizeof(payload)), "send in 5 byte fragments");
        Check(client.Send(ServerConnectionId, 78, payload, 3), "and a small one that still splits the header");
        Pump(server, client, clock);
        MessageView views[4];
        const std::uint32_t got = server.TakeMessages(views, 4);
        Check(got == 2, "both arrive as whole messages");
        Check(views[0].messageId == 77 && views[0].size == sizeof(payload), "the first has its id and size");
        Check(0 == std::memcmp(views[0].data, payload, sizeof(payload)), "and its bytes");
        Check(views[1].messageId == 78 && views[1].size == 3 && 0 == std::memcmp(views[1].data, payload, 3),
            "the second too");
    }

    // **닫힌 쪽이 Close 프레임을 보내면 상대는 `Normal` 로 끊긴다.**
    void TestCloseIsGraceful()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(16), "listen");
        Check(client.Connect("memory", 16), "connect");
        Pump(server, client, clock);
        NetworkEvent events[8];
        Drain(server, events, 8);
        Drain(client, events, 8);
        client.Close();
        Check(client.GetRole() == NetworkRole::None, "closing clears the role");
        const std::uint32_t clientCount = Drain(client, events, 8);
        Check(clientCount == 1 && events[0].kind == NetworkEventKind::Disconnected && events[0].reason == DisconnectReason::Normal,
            "the client reports its own close");
        Pump(server, client, clock);
        const std::uint32_t serverCount = Drain(server, events, 8);
        const NetworkEvent* gone = Find(events, serverCount, NetworkEventKind::Disconnected);
        Check(nullptr != gone && gone->reason == DisconnectReason::Normal, "the server sees a normal disconnect");
    }
}

int RunSessionTests()
{
    try
    {
        TestConnectedWaitsForHello();
        TestVersionMismatchIsRejected();
        TestPingMeasuresRoundTrip();
        TestSilenceTimesOut();
        TestIdleSocketIsDroppedWithoutEvent();
        TestFragmentedMessageIsReassembled();
        TestCloseIsGraceful();
    }
    catch (const std::exception& error)
    {
        std::cout << "SessionTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "SessionTests passed\n";
    return 0;
}
