#include "TestCheck.h"

#include <JBro/Network/Native/WinsockSocketProvider.h>
#include <JBro/Network/SteadyClock.h>
#include <JBro/Network/Transport.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // 실제 소켓은 시간이 걸린다. 조건이 참이 되거나 시한이 지날 때까지 양쪽을 돌린다.
    template <typename Predicate>
    bool PumpUntil(Transport& a, Transport& b, Predicate&& done, int timeoutMilliseconds = 5000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            a.Update();
            b.Update();
            if (done())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    // **루프백 위에서 실제 Winsock 으로 서버와 클라이언트가 붙고 메시지가 오간다.** 인메모리와 같은 트랜스포트 코드다.
    void TestLoopbackExchange()
    {
        Native::WinsockSocketProvider provider;
        Check(provider.IsAvailable(), "winsock starts");
        SteadyClock clock;
        Transport server(provider, clock);
        Transport client(provider, clock);

        // 포트가 다른 것에 잡혀 있을 수 있다. 몇 개를 시도한다.
        std::uint16_t port = 27717;
        bool listening = false;
        for (int attempt = 0; attempt < 16 && false == listening; ++attempt)
        {
            listening = server.Listen(static_cast<std::uint16_t>(port + attempt));
            if (listening)
            {
                port = static_cast<std::uint16_t>(port + attempt);
            }
        }
        Check(listening, "the server listens on a loopback port");
        Check(client.Connect("127.0.0.1", port), "the client connects to loopback");

        NetworkEvent events[8];
        std::uint32_t serverEvents = 0;
        std::uint32_t clientEvents = 0;
        const bool connected = PumpUntil(server, client, [&]()
        {
            serverEvents += server.TakeEvents(events + serverEvents, 8 - serverEvents);
            clientEvents += client.TakeEvents(events, 8);
            return serverEvents > 0 && clientEvents > 0;
        });
        Check(connected, "both sides report within the deadline");
        Check(events[0].kind == NetworkEventKind::Connected || serverEvents > 0, "and it is Connected");
        Check(server.GetConnectionCount() == 1, "one connection");
        const ConnectionId clientOnServer = server.GetConnectionAt(0);
        Check(server.GetConnectionState(clientOnServer) == ConnectionState::Connected, "connected on the server");
        Check(client.GetConnectionState(ServerConnectionId) == ConnectionState::Connected, "and on the client");

        const char text[] = "over real sockets";
        Check(client.Send(ServerConnectionId, 5, text, sizeof(text)), "the client sends");
        MessageView view;
        const bool received = PumpUntil(server, client, [&]()
        {
            return server.TakeMessages(&view, 1) == 1;
        });
        Check(received, "the server receives it");
        Check(view.messageId == 5 && view.size == sizeof(text) && 0 == std::memcmp(view.data, text, sizeof(text)),
            "intact");

        std::uint8_t big[40000];
        for (std::uint32_t index = 0; index < sizeof(big); ++index)
        {
            big[index] = static_cast<std::uint8_t>(index * 13);
        }
        Check(server.Send(clientOnServer, 6, big, sizeof(big)), "the server sends 40000 bytes");
        MessageView bigView;
        const bool bigReceived = PumpUntil(server, client, [&]()
        {
            return client.TakeMessages(&bigView, 1) == 1;
        });
        Check(bigReceived, "the client receives the large message");
        Check(bigView.size == sizeof(big) && 0 == std::memcmp(bigView.data, big, sizeof(big)), "whole and in order");

        server.CloseConnection(clientOnServer);
        const bool closed = PumpUntil(server, client, [&]()
        {
            return client.GetRole() == NetworkRole::None;
        });
        Check(closed, "the client notices the server closing");
    }

    // **아무도 듣지 않는 포트에 붙으면 `Disconnected(Error)` 가 온다.** 실제 소켓은 비동기로 거부된다.
    void TestConnectionRefused()
    {
        Native::WinsockSocketProvider provider;
        SteadyClock clock;
        Transport client(provider, clock);
        Transport nobody(provider, clock);
        Check(client.Connect("127.0.0.1", 1), "connecting to port 1 starts");
        NetworkEvent event;
        std::uint32_t count = 0;
        const bool refused = PumpUntil(client, nobody, [&]()
        {
            count = client.TakeEvents(&event, 1);
            return count == 1;
        });
        Check(refused, "the refusal arrives");
        Check(event.kind == NetworkEventKind::Disconnected && event.reason == DisconnectReason::Error, "as an error");
        Check(client.GetRole() == NetworkRole::None, "and the role is cleared");
    }
}

int RunWinsockLoopbackTests()
{
    try
    {
        TestLoopbackExchange();
        TestConnectionRefused();
    }
    catch (const std::exception& error)
    {
        std::cout << "WinsockLoopbackTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "WinsockLoopbackTests passed\n";
    return 0;
}
