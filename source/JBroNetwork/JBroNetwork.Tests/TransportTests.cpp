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
    // 양쪽을 몇 번 돌려 파이프를 비운다. 인메모리 파이프는 한 번의 Update 로 한 홉만 나아간다.
    void Pump(Transport& a, Transport& b, int rounds = 8)
    {
        for (int round = 0; round < rounds; ++round)
        {
            a.Update();
            b.Update();
        }
    }

    struct EventLog
    {
        NetworkEvent events[64];
        std::uint32_t count = 0;

        void Drain(Transport& transport)
        {
            count += transport.TakeEvents(events + count, 64 - count);
        }

        std::uint32_t CountKind(NetworkEventKind kind) const
        {
            std::uint32_t total = 0;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (events[index].kind == kind)
                {
                    ++total;
                }
            }
            return total;
        }

        const NetworkEvent* Find(NetworkEventKind kind) const
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
    };

    // **서버와 클라이언트가 한 프로세스에서 이어지고, 메시지가 ID·채널·바이트 그대로 양방향으로 오간다.**
    // 트랜스포트에 전역 상태가 없다는 것이 이 테스트의 전제다 - 있었다면 두 인스턴스가 서로를 밟았을 것이다.
    void TestConnectAndExchange()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);

        Check(server.Listen(7777), "the server listens");
        Check(server.GetRole() == NetworkRole::Server, "and takes the server role");
        Check(client.Connect("memory", 7777), "the client starts connecting");
        Check(client.GetRole() == NetworkRole::Client, "and takes the client role");
        Check(client.GetConnectionState(ServerConnectionId) == ConnectionState::Connecting,
            "the connection is pending until the server accepts");

        Pump(server, client);

        EventLog serverEvents;
        EventLog clientEvents;
        serverEvents.Drain(server);
        clientEvents.Drain(client);
        Check(serverEvents.CountKind(NetworkEventKind::Connected) == 1, "the server sees one connection");
        Check(serverEvents.events[0].connection == ServerConnectionId + 1, "numbered from 2");
        Check(clientEvents.CountKind(NetworkEventKind::Connected) == 1, "the client sees the server");
        Check(clientEvents.events[0].connection == ServerConnectionId, "as connection 1");
        Check(client.GetConnectionState(ServerConnectionId) == ConnectionState::Connected, "and is connected");
        Check(server.GetConnectionCount() == 1, "the server holds one connection");

        const char hello[] = "hello";
        Check(client.Send(ServerConnectionId, 7, hello, sizeof(hello), NetChannel::Unreliable), "the client sends");
        Pump(server, client);

        MessageView views[4];
        const std::uint32_t received = server.TakeMessages(views, 4);
        Check(received == 1, "the server takes exactly one message");
        Check(views[0].connection == ServerConnectionId + 1, "from connection 2");
        Check(views[0].messageId == 7, "with the message id it was sent with");
        Check(views[0].channel == NetChannel::ReliableOrdered,
            "it arrived on the reliable channel - WS carries every channel until UDP exists");
        Check(views[0].size == sizeof(hello), "and the size");
        Check(0 == std::memcmp(views[0].data, hello, sizeof(hello)), "and the bytes");

        const std::uint32_t reply = 0xCAFEF00D;
        Check(server.Broadcast(9, &reply, sizeof(reply)), "the server broadcasts");
        Pump(server, client);
        const std::uint32_t clientReceived = client.TakeMessages(views, 4);
        Check(clientReceived == 1, "the client takes the broadcast");
        Check(views[0].connection == ServerConnectionId, "from the server");
        Check(views[0].messageId == 9, "with its id");
        Check(views[0].channel == NetChannel::ReliableOrdered, "on the default channel");
        std::uint32_t decoded = 0;
        std::memcpy(&decoded, views[0].data, sizeof(decoded));
        Check(decoded == reply, "and the payload intact");

        Check(false == client.Broadcast(1, nullptr, 0), "a client cannot broadcast");
        Check(server.TakeMessages(views, 4) == 0, "nothing else is queued");
    }

    // **큰 메시지는 파이프가 좁아도 여러 번의 Update 에 걸쳐 온전히 도착한다.** 부분 송신과 부분 수신을 둘 다 겪게 한다.
    void TestLargeMessageCrossesNarrowPipe()
    {
        MemorySocketProvider provider(1024);
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(1), "listen");
        Check(client.Connect("memory", 1), "connect");
        Pump(server, client);

        std::uint8_t payload[20000];
        for (std::uint32_t index = 0; index < sizeof(payload); ++index)
        {
            payload[index] = static_cast<std::uint8_t>(index * 7 + 3);
        }
        Check(client.Send(ServerConnectionId, 21, payload, sizeof(payload)), "a 20000 byte message is accepted");

        MessageView view;
        std::uint32_t got = 0;
        for (int round = 0; round < 200 && 0 == got; ++round)
        {
            Pump(server, client, 1);
            got = server.TakeMessages(&view, 1);
        }
        Check(got == 1, "the whole message eventually arrives");
        Check(view.size == sizeof(payload), "with its full size");
        Check(0 == std::memcmp(view.data, payload, sizeof(payload)), "and every byte in order");
    }

    // **닫기는 다음 Update 끝에서 정리되고 양쪽이 `Disconnected` 를 본다.** 순회 중에 지우지 않는다는 규약의 관측이다.
    void TestCloseConnectionIsSeenByBothSides()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(2), "listen");
        Check(client.Connect("memory", 2), "connect");
        Pump(server, client);
        EventLog drop;
        drop.Drain(server);
        drop.Drain(client);

        const ConnectionId clientOnServer = server.GetConnectionAt(0);
        server.CloseConnection(clientOnServer);
        Check(server.GetConnectionCount() == 1, "the connection is still there until Update");
        Pump(server, client);

        EventLog serverEvents;
        EventLog clientEvents;
        serverEvents.Drain(server);
        clientEvents.Drain(client);
        const NetworkEvent* serverSide = serverEvents.Find(NetworkEventKind::Disconnected);
        const NetworkEvent* clientSide = clientEvents.Find(NetworkEventKind::Disconnected);
        Check(nullptr != serverSide, "the server reports the disconnect");
        Check(serverSide->connection == clientOnServer, "for that connection");
        Check(serverSide->reason == DisconnectReason::Normal, "as a normal close");
        Check(nullptr != clientSide, "the client reports the disconnect too");
        Check(clientSide->reason == DisconnectReason::Normal, "as the peer closing normally");
        Check(server.GetConnectionCount() == 0, "the server has no connections left");
        Check(client.GetRole() == NetworkRole::None, "and the client has no role left");
        Check(false == client.Send(ServerConnectionId, 1, nullptr, 0), "so it cannot send");
    }

    // **받는 이가 없는 접속은 시작조차 못 한다.** 실제 소켓은 비동기로 거부되지만 인메모리는 그 자리에서 안다.
    void TestConnectWithoutListenerFails()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport client(provider, clock);
        Check(false == client.Connect("memory", 4242), "connecting to nobody fails");
        Check(client.GetRole() == NetworkRole::None, "and leaves no role behind");
    }

    // **이벤트 큐가 넘치면 버린 뒤 `Overflow` 하나로 알린다.** 받는 쪽은 그것을 보고 상태를 다시 조회한다.
    void TestEventOverflowIsReported()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        TransportConfig small;
        small.eventCapacity = 2;
        Transport server(provider, clock, small);
        Transport clients[4] = {
            Transport(provider, clock), Transport(provider, clock), Transport(provider, clock), Transport(provider, clock) };
        Check(server.Listen(3), "listen");
        for (Transport& client : clients)
        {
            Check(client.Connect("memory", 3), "connect");
        }
        for (int round = 0; round < 8; ++round)
        {
            server.Update();
            for (Transport& client : clients)
            {
                client.Update();
            }
        }
        Check(server.GetConnectionCount() == 4, "all four connected regardless of the queue");

        NetworkEvent events[8];
        std::uint32_t first = server.TakeEvents(events, 8);
        Check(first == 2, "only two events fit");
        std::uint32_t second = server.TakeEvents(events, 8);
        Check(second == 1, "then the overflow marker follows");
        Check(events[0].kind == NetworkEventKind::Overflow, "and it is an Overflow");
        Check(server.TakeEvents(events, 8) == 0, "and nothing more");
    }

    // **꺼내 가지 않은 메시지는 Update 를 지나도 남고, 꺼내 간 것만 비운다.** 압축이 오프셋을 제대로 옮기는지 본다.
    void TestUntakenMessagesSurviveUpdate()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);
        Check(server.Listen(5), "listen");
        Check(client.Connect("memory", 5), "connect");
        Pump(server, client);

        const char first[] = "first";
        const char second[] = "second!";
        const char third[] = "3";
        Check(client.Send(ServerConnectionId, 1, first, sizeof(first)), "send 1");
        Check(client.Send(ServerConnectionId, 2, second, sizeof(second)), "send 2");
        Check(client.Send(ServerConnectionId, 3, third, sizeof(third)), "send 3");
        Pump(server, client);

        MessageView view;
        Check(server.TakeMessages(&view, 1) == 1, "take the first");
        Check(view.messageId == 1 && 0 == std::memcmp(view.data, first, sizeof(first)), "it is 'first'");
        server.Update();
        Check(server.TakeMessages(&view, 1) == 1, "the second is still there after Update");
        Check(view.messageId == 2, "with id 2");
        Check(view.size == sizeof(second), "its size");
        Check(0 == std::memcmp(view.data, second, sizeof(second)), "and its bytes moved intact");
        server.Update();
        Check(server.TakeMessages(&view, 1) == 1, "and the third");
        Check(view.messageId == 3 && 0 == std::memcmp(view.data, third, sizeof(third)), "is '3'");
        server.Update();
        Check(server.TakeMessages(&view, 1) == 0, "then the queue is empty");
    }

    // **수신 저장소가 차면 메시지를 버리지 않고 소켓 읽기를 멈춘다.** 자리가 나면 이어서 받는다. 이것이 신뢰 채널의 역압이다.
    void TestInboundBackpressureDropsNothing()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        TransportConfig tight;
        tight.inboundMessages = 2;
        Transport server(provider, clock, tight);
        Transport client(provider, clock);
        Check(server.Listen(6), "listen");
        Check(client.Connect("memory", 6), "connect");
        Pump(server, client);

        for (std::uint16_t id = 1; id <= 6; ++id)
        {
            Check(client.Send(ServerConnectionId, id, &id, sizeof(id)), "send");
        }
        Pump(server, client, 8);

        std::uint16_t expected = 1;
        MessageView views[2];
        for (int round = 0; round < 6; ++round)
        {
            const std::uint32_t got = server.TakeMessages(views, 2);
            for (std::uint32_t index = 0; index < got; ++index)
            {
                Check(views[index].messageId == expected, "messages arrive in order without gaps");
                ++expected;
            }
            server.Update();
        }
        Check(expected == 7, "all six were delivered despite room for only two at a time");
    }

    // **큰 프레임을 주장하는 헤더는 오류로 끊는다.** 한계를 넘는 길이는 저장소를 영원히 기다리게 만들 것이다.
    void TestOversizedFrameDisconnectsWithError()
    {
        MemorySocketProvider provider;
        ManualClock clock;
        TransportConfig config;
        config.maxMessageBytes = 16;
        Transport server(provider, clock, config);
        Transport client(provider, clock);
        Check(server.Listen(8), "listen");
        Check(client.Connect("memory", 8), "connect");
        Pump(server, client);
        EventLog drop;
        drop.Drain(server);

        std::uint8_t big[64] = {};
        Check(client.Send(ServerConnectionId, 1, big, sizeof(big)), "the client, with its own larger limit, sends 64 bytes");
        Pump(server, client);
        EventLog events;
        events.Drain(server);
        const NetworkEvent* disconnected = events.Find(NetworkEventKind::Disconnected);
        Check(nullptr != disconnected, "the server drops the connection");
        Check(disconnected->reason == DisconnectReason::Error, "as a protocol error");
    }
}

int RunTransportTests()
{
    try
    {
        TestConnectAndExchange();
        TestLargeMessageCrossesNarrowPipe();
        TestCloseConnectionIsSeenByBothSides();
        TestConnectWithoutListenerFails();
        TestEventOverflowIsReported();
        TestUntakenMessagesSurviveUpdate();
        TestInboundBackpressureDropsNothing();
        TestOversizedFrameDisconnectsWithError();
    }
    catch (const std::exception& error)
    {
        std::cout << "TransportTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "TransportTests passed\n";
    return 0;
}
