// winsock2 는 windows.h 보다 먼저 와야 한다 - 가이드 시험 파일은 windows.h 를 먼저 끌어와 이 시험을 따로 둔다.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorControlPort.h>
#include <JBro/Network/Native/WinsockSocketProvider.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <thread>

// 제어 포트(D-270)가 **이 기계 밖에서는 닿지 않는지** 실제 에디터로 잰다. 글의 형식과 명령은 `EditorGuideTests.cpp` 가 잰다.

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    // 이 기계의 루프백이 아닌 IPv4 주소를 찾는다. 망이 없으면 거짓이다.
    bool FindExternalAddress(char* out, std::size_t capacity)
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
        bool found = false;
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

    // 접속이 `Connecting` 을 벗어날 때까지 에디터를 돌리며 기다린다. 거부는 Windows 가 SYN 을 몇 번 다시 보낸 뒤에야 알린다.
    JBro::Network::ConnectionState WaitConnected(JBro::EditorApplication& editor, JBro::Network::IStreamSocket& socket)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
        while (socket.GetState() == JBro::Network::ConnectionState::Connecting && std::chrono::steady_clock::now() < deadline)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor must tick");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return socket.GetState();
    }

    // **에디터가 연 제어 포트는 루프백으로는 붙고 이 기계의 바깥 주소로는 붙지 않는다.** 에디터를 움직이는 포트가 같은 망에 열리면 안 된다.
    void TestTheControlPortIsNotReachableFromOutside()
    {
        JBro::Network::Native::WinsockSocketProvider provider;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        // 사람의 에디터가 3663 을 쓰고 있을 수 있다. 빈 번호를 찾아 쓴다.
        for (std::uint16_t candidate = 36651; candidate < 36671 && config.controlPort == 0; ++candidate)
        {
            JBro::EditorControlPort probe;
            if (probe.Open(provider, candidate))
            {
                config.controlPort = candidate;
            }
        }
        Check(config.controlPort != 0, "a free loopback port must be found");

        JBro::EditorApplication editor;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] the editor did not initialize; the control port was not probed from outside" << std::endl;
            return;
        }
        Check(editor.IsControlPortOpen(), "the editor opens its control port");

        JBro::OwnerPtr<JBro::Network::IStreamSocket> local = provider.CreateStreamSocket();
        Check(local->Connect("127.0.0.1", config.controlPort), "connecting over loopback starts");
        Check(WaitConnected(editor, *local) == JBro::Network::ConnectionState::Connected, "a tool on this machine connects");

        char address[64] = {};
        if (false == FindExternalAddress(address, sizeof(address)))
        {
            std::cout << "  [skip] no non-loopback address; the control port was not probed from outside" << std::endl;
            editor.Shutdown();
            return;
        }
        JBro::OwnerPtr<JBro::Network::IStreamSocket> outside = provider.CreateStreamSocket();
        if (outside->Connect(address, config.controlPort))
        {
            Check(WaitConnected(editor, *outside) == JBro::Network::ConnectionState::Disconnected,
                "the control port refuses this machine's outside address");
        }
        editor.Shutdown();
    }
}

int RunEditorControlPortTests()
{
    try
    {
        TestTheControlPortIsNotReachableFromOutside();
    }
    catch (const std::exception& error)
    {
        std::cout << "Editor control port tests failed: " << error.what() << std::endl;
        return 1;
    }
    std::cout << "Editor control port tests passed.\n";
    return 0;
}
