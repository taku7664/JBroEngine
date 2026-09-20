#pragma once

#include <JBro/Network/Socket.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Network::Native
{
    // Winsock2 위의 논블로킹 소켓이다. `<winsock2.h>` 는 .cpp 에만 들어간다. 소켓 핸들은 `uintptr_t` 로 든다.
    // 독립 개발 단계에서는 테스트가 이것을 직접 넘기고, 부착 단계에서 Windows 플랫폼이 이것을 돌려준다.
    // WSAStartup 은 인스턴스 단위로 짝을 맞춘다 - OS 가 참조 수를 세므로 provider 가 여럿이어도 안전하다.
    class WinsockSocketProvider final : public ISocketProvider
    {
    public:
        WinsockSocketProvider();
        ~WinsockSocketProvider() override;

        OwnerPtr<IStreamSocket> CreateStreamSocket() override;
        OwnerPtr<IDatagramSocket> CreateDatagramSocket() override;
        OwnerPtr<IPeerConnection> CreatePeerConnection(const PeerConnectionDesc& desc) override;

        bool IsAvailable() const;

    private:
        bool m_started = false;
    };

    class WinsockStreamSocket final : public IStreamSocket
    {
    public:
        WinsockStreamSocket();
        explicit WinsockStreamSocket(std::uintptr_t acceptedSocket);
        ~WinsockStreamSocket() override;

        bool Connect(const char* host, std::uint16_t port) override;
        bool Listen(std::uint16_t port) override;
        OwnerPtr<IStreamSocket> Accept() override;
        ConnectionState GetState() const override;
        SocketIo Send(const void* data, std::size_t size, std::size_t& outSent) override;
        SocketIo Receive(void* buffer, std::size_t capacity, std::size_t& outReceived) override;
        void Close() override;

    private:
        bool EnsureSocket();
        void SetNonBlocking();

        static constexpr std::uintptr_t InvalidHandle = ~static_cast<std::uintptr_t>(0);
        std::uintptr_t m_socket = InvalidHandle;
        // 접속 결과는 select 로 알아본다. GetState 가 const 라 판정을 여기에 적어 둔다.
        mutable ConnectionState m_state = ConnectionState::Disconnected;
    };

    class WinsockDatagramSocket final : public IDatagramSocket
    {
    public:
        WinsockDatagramSocket() = default;
        ~WinsockDatagramSocket() override;

        bool Open() override;
        bool Bind(std::uint16_t port) override;
        bool Resolve(const char* host, std::uint16_t port, Endpoint& outEndpoint) override;
        SocketIo SendTo(const Endpoint& to, const void* data, std::size_t size) override;
        SocketIo ReceiveFrom(void* buffer, std::size_t capacity, std::size_t& outReceived, Endpoint& outFrom) override;
        void Close() override;
        bool IsOpen() const override;

    private:
        void SetNonBlocking();

        static constexpr std::uintptr_t InvalidHandle = ~static_cast<std::uintptr_t>(0);
        std::uintptr_t m_socket = InvalidHandle;
    };
}
