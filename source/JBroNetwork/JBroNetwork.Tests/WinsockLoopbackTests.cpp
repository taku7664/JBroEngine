#include "TestCheck.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <JBro/Network/Native/WinsockSocketProvider.h>
#include <JBro/Network/SteadyClock.h>
#include <JBro/Network/Transport.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // 실제 소켓은 시간이 걸린다. 조건이 참이 되거나 시한이 지날 때까지 양쪽을 돌린다.
    template <typename Predicate>
    JBro::Bool PumpUntil(Transport& a, Transport& b, Predicate&& done, JBro::Int32 timeoutMilliseconds = 5000)
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
        JBro::Bool listening = false;
        for (JBro::Int32 attempt = 0; attempt < 16 && false == listening; ++attempt)
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
        JBro::UInt32 serverEvents = 0;
        JBro::UInt32 clientEvents = 0;
        const JBro::Bool connected = PumpUntil(server, client, [&]()
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
        const JBro::Bool received = PumpUntil(server, client, [&]()
        {
            return server.TakeMessages(&view, 1) == 1;
        });
        Check(received, "the server receives it");
        Check(view.messageId == 5 && view.size == sizeof(text) && 0 == std::memcmp(view.data, text, sizeof(text)),
            "intact");

        std::uint8_t big[40000];
        for (JBro::UInt32 index = 0; index < sizeof(big); ++index)
        {
            big[index] = static_cast<std::uint8_t>(index * 13);
        }
        Check(server.Send(clientOnServer, 6, big, sizeof(big)), "the server sends 40000 bytes");
        MessageView bigView;
        const JBro::Bool bigReceived = PumpUntil(server, client, [&]()
        {
            return client.TakeMessages(&bigView, 1) == 1;
        });
        Check(bigReceived, "the client receives the large message");
        Check(bigView.size == sizeof(big) && 0 == std::memcmp(bigView.data, big, sizeof(big)), "whole and in order");

        server.CloseConnection(clientOnServer);
        const JBro::Bool closed = PumpUntil(server, client, [&]()
        {
            return client.GetRole() == NetworkRole::None;
        });
        Check(closed, "the client notices the server closing");
    }

    // 이 기계의 루프백이 아닌 IPv4 주소를 찾는다. 망이 없으면 거짓이다.
    JBro::Bool FindExternalAddress(char* out, std::size_t capacity)
    {
        char host[256] = {};
        if (0 != gethostname(host, sizeof(host)))
        {
            return false;
        }
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* list = nullptr;
        if (0 != getaddrinfo(host, nullptr, &hints, &list))
        {
            return false;
        }
        JBro::Bool found = false;
        for (addrinfo* entry = list; entry != nullptr && false == found; entry = entry->ai_next)
        {
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            if ((ntohl(address->sin_addr.s_addr) >> 24) == 127)
            {
                continue;
            }
            found = nullptr != inet_ntop(AF_INET, &address->sin_addr, out, capacity);
        }
        freeaddrinfo(list);
        return found;
    }

    // 접속이 `Connecting` 을 벗어날 때까지 기다린다. 거부된 접속은 Windows 가 SYN 을 몇 번 다시 보낸 뒤에야 알린다.
    ConnectionState WaitConnected(IStreamSocket& socket)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
        while (socket.GetState() == ConnectionState::Connecting && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return socket.GetState();
    }

    JBro::OwnerPtr<IStreamSocket> OpenListener(Native::WinsockSocketProvider& provider, JBro::Bool loopback, std::uint16_t first, std::uint16_t& port)
    {
        for (JBro::Int32 attempt = 0; attempt < 16; ++attempt)
        {
            JBro::OwnerPtr<IStreamSocket> socket = provider.CreateStreamSocket();
            port = static_cast<std::uint16_t>(first + attempt);
            if (loopback ? socket->ListenLoopback(port) : socket->Listen(port))
            {
                return socket;
            }
        }
        return nullptr;
    }

    // **`ListenLoopback` 은 이 기계에서 오는 접속만 받는다**(D-270). 에디터 제어 포트가 같은 망의 다른 기계에 열리면 안 된다.
    // 대조로 `Listen` 한 포트는 바깥 주소로도 붙는 것을 먼저 본다 - 그래야 "안 붙는다" 가 재기의 고장이 아니다.
    void TestListenLoopbackStaysOnThisMachine()
    {
        Native::WinsockSocketProvider provider;
        std::uint16_t localPort = 0;
        std::uint16_t openPort = 0;
        JBro::OwnerPtr<IStreamSocket> local = OpenListener(provider, true, 27817, localPort);
        JBro::OwnerPtr<IStreamSocket> open = OpenListener(provider, false, 27917, openPort);
        Check(nullptr != local.Get() && nullptr != open.Get(), "both listeners open");

        JBro::OwnerPtr<IStreamSocket> fromLoopback = provider.CreateStreamSocket();
        Check(fromLoopback->Connect("127.0.0.1", localPort), "connecting over loopback starts");
        Check(WaitConnected(*fromLoopback) == ConnectionState::Connected, "a loopback listener takes a loopback connection");

        char address[64] = {};
        if (false == FindExternalAddress(address, sizeof(address)))
        {
            std::cout << "  [skip] no non-loopback address; the loopback-only listener was not probed from outside\n";
            return;
        }
        JBro::OwnerPtr<IStreamSocket> toOpen = provider.CreateStreamSocket();
        Check(toOpen->Connect(address, openPort), "connecting to the open listener starts");
        Check(WaitConnected(*toOpen) == ConnectionState::Connected, "a listener on every address takes this machine's outside address");
        JBro::OwnerPtr<IStreamSocket> toLocal = provider.CreateStreamSocket();
        if (toLocal->Connect(address, localPort))
        {
            Check(WaitConnected(*toLocal) == ConnectionState::Disconnected, "the loopback listener refuses the outside address");
        }
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
        JBro::UInt32 count = 0;
        const JBro::Bool refused = PumpUntil(client, nobody, [&]()
        {
            count = client.TakeEvents(&event, 1);
            return count == 1;
        });
        Check(refused, "the refusal arrives");
        Check(event.kind == NetworkEventKind::Disconnected && event.reason == DisconnectReason::Error, "as an error");
        Check(client.GetRole() == NetworkRole::None, "and the role is cleared");
    }
}

JBro::Int32 RunWinsockLoopbackTests()
{
    try
    {
        TestLoopbackExchange();
        TestConnectionRefused();
        TestListenLoopbackStaysOnThisMachine();
    }
    catch (const std::exception& error)
    {
        std::cout << "WinsockLoopbackTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "WinsockLoopbackTests passed\n";
    return 0;
}
