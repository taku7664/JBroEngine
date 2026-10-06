#include <JBro/Network/Native/WinsockSocketProvider.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Int.h>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdio>
#include <cstring>

namespace JBro::Network::Native
{
    namespace
    {
        SOCKET ToSocket(std::uintptr_t value)
        {
            return static_cast<SOCKET>(value);
        }

        std::uintptr_t FromSocket(SOCKET socket)
        {
            return static_cast<std::uintptr_t>(socket);
        }

        Bool ResolveAddress(const char* host, std::uint16_t port, Int32 socketType, Int32 protocol, Endpoint& out)
        {
            if (nullptr == host)
            {
                return false;
            }
            char portText[8];
            std::snprintf(portText, sizeof(portText), "%u", static_cast<unsigned>(port));
            addrinfo hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = socketType;
            hints.ai_protocol = protocol;
            addrinfo* results = nullptr;
            if (0 != getaddrinfo(host, portText, &hints, &results) || nullptr == results)
            {
                return false;
            }
            if (results->ai_addrlen > Endpoint::Capacity)
            {
                freeaddrinfo(results);
                return false;
            }
            std::memcpy(out.data, results->ai_addr, results->ai_addrlen);
            out.length = static_cast<JBro::UInt32>(results->ai_addrlen);
            freeaddrinfo(results);
            return true;
        }
    }

    // ── provider ────────────────────────────────────────────────────────────────────────────────

    WinsockSocketProvider::WinsockSocketProvider()
    {
        WSADATA data;
        m_started = 0 == WSAStartup(MAKEWORD(2, 2), &data);
    }

    WinsockSocketProvider::~WinsockSocketProvider()
    {
        if (m_started)
        {
            WSACleanup();
        }
    }

    OwnerPtr<IStreamSocket> WinsockSocketProvider::CreateStreamSocket()
    {
        if (false == m_started)
        {
            return nullptr;
        }
        return MakeOwnerPtr<WinsockStreamSocket>();
    }

    OwnerPtr<IDatagramSocket> WinsockSocketProvider::CreateDatagramSocket()
    {
        if (false == m_started)
        {
            return nullptr;
        }
        return MakeOwnerPtr<WinsockDatagramSocket>();
    }

    OwnerPtr<IPeerConnection> WinsockSocketProvider::CreatePeerConnection(const PeerConnectionDesc& desc)
    {
        (void)desc;
        // 네이티브 WebRTC 스택은 열어 둔 항목이다(network-plan §2.7).
        return nullptr;
    }

    Bool WinsockSocketProvider::IsAvailable() const
    {
        return m_started;
    }

    // ── 스트림 소켓 ─────────────────────────────────────────────────────────────────────────────

    WinsockStreamSocket::WinsockStreamSocket() = default;

    WinsockStreamSocket::WinsockStreamSocket(std::uintptr_t acceptedSocket)
        : m_socket(acceptedSocket)
        , m_state(ConnectionState::Connected)
    {
        SetNonBlocking();
    }

    WinsockStreamSocket::~WinsockStreamSocket()
    {
        Close();
    }

    Bool WinsockStreamSocket::EnsureSocket()
    {
        if (InvalidHandle != m_socket)
        {
            return true;
        }
        const SOCKET created = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (INVALID_SOCKET == created)
        {
            return false;
        }
        m_socket = FromSocket(created);
        SetNonBlocking();
        return true;
    }

    void WinsockStreamSocket::SetNonBlocking()
    {
        if (InvalidHandle == m_socket)
        {
            return;
        }
        u_long mode = 1;
        ioctlsocket(ToSocket(m_socket), FIONBIO, &mode);
    }

    Bool WinsockStreamSocket::Connect(const char* host, std::uint16_t port)
    {
        Endpoint endpoint;
        if (false == ResolveAddress(host, port, SOCK_STREAM, static_cast<Int32>(IPPROTO_TCP), endpoint))
        {
            return false;
        }
        if (false == EnsureSocket())
        {
            return false;
        }
        const Int32 result = connect(ToSocket(m_socket), reinterpret_cast<const sockaddr*>(endpoint.data),
            static_cast<JBro::Int32>(endpoint.length));
        if (0 != result && WSAEWOULDBLOCK != WSAGetLastError())
        {
            Close();
            return false;
        }
        m_state = 0 == result ? ConnectionState::Connected : ConnectionState::Connecting;
        return true;
    }

    Bool WinsockStreamSocket::Listen(std::uint16_t port)
    {
        return ListenOn(INADDR_ANY, port);
    }

    Bool WinsockStreamSocket::ListenLoopback(std::uint16_t port)
    {
        return ListenOn(INADDR_LOOPBACK, port);
    }

    Bool WinsockStreamSocket::ListenOn(UInt32 hostAddress, std::uint16_t port)
    {
        if (false == EnsureSocket())
        {
            return false;
        }
        // SO_REUSEADDR 는 두지 않는다 - 같은 포트에 서버 둘이 붙는 것을 테스트가 알아채야 한다.
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(hostAddress);
        address.sin_port = htons(port);
        if (0 != bind(ToSocket(m_socket), reinterpret_cast<sockaddr*>(&address), sizeof(address)))
        {
            Close();
            return false;
        }
        if (0 != listen(ToSocket(m_socket), SOMAXCONN))
        {
            Close();
            return false;
        }
        m_state = ConnectionState::Connected;
        return true;
    }

    OwnerPtr<IStreamSocket> WinsockStreamSocket::Accept()
    {
        if (InvalidHandle == m_socket)
        {
            return nullptr;
        }
        sockaddr_in clientAddress = {};
        int addressLength = sizeof(clientAddress);
        const SOCKET client = accept(ToSocket(m_socket), reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
        if (INVALID_SOCKET == client)
        {
            return nullptr;
        }
        return MakeOwnerPtr<WinsockStreamSocket>(FromSocket(client));
    }

    ConnectionState WinsockStreamSocket::GetState() const
    {
        if (InvalidHandle == m_socket)
        {
            return ConnectionState::Disconnected;
        }
        if (m_state != ConnectionState::Connecting)
        {
            return m_state;
        }
        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(ToSocket(m_socket), &writeSet);
        FD_SET(ToSocket(m_socket), &errorSet);
        timeval immediately = { 0, 0 };
        const Int32 ready = select(0, nullptr, &writeSet, &errorSet, &immediately);
        if (ready <= 0)
        {
            return ConnectionState::Connecting;
        }
        if (FD_ISSET(ToSocket(m_socket), &errorSet))
        {
            m_state = ConnectionState::Disconnected;
        }
        else if (FD_ISSET(ToSocket(m_socket), &writeSet))
        {
            m_state = ConnectionState::Connected;
        }
        return m_state;
    }

    SocketIo WinsockStreamSocket::Send(const void* data, std::size_t size, std::size_t& outSent)
    {
        outSent = 0;
        if (InvalidHandle == m_socket)
        {
            return SocketIo::Error;
        }
        if (0 == size)
        {
            return SocketIo::Ok;
        }
        const Int32 sent = send(ToSocket(m_socket), static_cast<const char*>(data), static_cast<JBro::Int32>(size), 0);
        if (sent > 0)
        {
            outSent = static_cast<std::size_t>(sent);
            return SocketIo::Ok;
        }
        const Int32 error = WSAGetLastError();
        if (WSAEWOULDBLOCK == error)
        {
            return SocketIo::WouldBlock;
        }
        if (WSAECONNRESET == error || WSAECONNABORTED == error || WSAESHUTDOWN == error)
        {
            return SocketIo::Closed;
        }
        return SocketIo::Error;
    }

    SocketIo WinsockStreamSocket::Receive(void* buffer, std::size_t capacity, std::size_t& outReceived)
    {
        outReceived = 0;
        if (InvalidHandle == m_socket || 0 == capacity)
        {
            return SocketIo::Error;
        }
        const Int32 received = recv(ToSocket(m_socket), static_cast<char*>(buffer), static_cast<JBro::Int32>(capacity), 0);
        if (received > 0)
        {
            outReceived = static_cast<std::size_t>(received);
            return SocketIo::Ok;
        }
        if (0 == received)
        {
            return SocketIo::Closed;
        }
        const Int32 error = WSAGetLastError();
        if (WSAEWOULDBLOCK == error)
        {
            return SocketIo::WouldBlock;
        }
        if (WSAECONNRESET == error || WSAECONNABORTED == error)
        {
            return SocketIo::Closed;
        }
        return SocketIo::Error;
    }

    void WinsockStreamSocket::Close()
    {
        if (InvalidHandle != m_socket)
        {
            closesocket(ToSocket(m_socket));
            m_socket = InvalidHandle;
        }
        m_state = ConnectionState::Disconnected;
    }

    // ── 데이터그램 소켓 ─────────────────────────────────────────────────────────────────────────

    WinsockDatagramSocket::~WinsockDatagramSocket()
    {
        Close();
    }

    Bool WinsockDatagramSocket::Open()
    {
        if (InvalidHandle != m_socket)
        {
            return true;
        }
        const SOCKET created = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (INVALID_SOCKET == created)
        {
            return false;
        }
        m_socket = FromSocket(created);
        SetNonBlocking();
        return true;
    }

    void WinsockDatagramSocket::SetNonBlocking()
    {
        if (InvalidHandle == m_socket)
        {
            return;
        }
        u_long mode = 1;
        ioctlsocket(ToSocket(m_socket), FIONBIO, &mode);
    }

    Bool WinsockDatagramSocket::Bind(std::uint16_t port)
    {
        if (InvalidHandle == m_socket && false == Open())
        {
            return false;
        }
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        return 0 == bind(ToSocket(m_socket), reinterpret_cast<sockaddr*>(&address), sizeof(address));
    }

    Bool WinsockDatagramSocket::Resolve(const char* host, std::uint16_t port, Endpoint& outEndpoint)
    {
        return ResolveAddress(host, port, SOCK_DGRAM, static_cast<Int32>(IPPROTO_UDP), outEndpoint);
    }

    SocketIo WinsockDatagramSocket::SendTo(const Endpoint& to, const void* data, std::size_t size)
    {
        if (InvalidHandle == m_socket || false == to.IsValid())
        {
            return SocketIo::Error;
        }
        const Int32 sent = sendto(ToSocket(m_socket), static_cast<const char*>(data), static_cast<JBro::Int32>(size), 0,
            reinterpret_cast<const sockaddr*>(to.data), static_cast<JBro::Int32>(to.length));
        if (sent >= 0)
        {
            return SocketIo::Ok;
        }
        if (WSAEWOULDBLOCK == WSAGetLastError())
        {
            return SocketIo::WouldBlock;
        }
        return SocketIo::Error;
    }

    SocketIo WinsockDatagramSocket::ReceiveFrom(void* buffer, std::size_t capacity, std::size_t& outReceived, Endpoint& outFrom)
    {
        outReceived = 0;
        if (InvalidHandle == m_socket)
        {
            return SocketIo::Error;
        }
        int fromLength = static_cast<JBro::Int32>(Endpoint::Capacity);
        const Int32 received = recvfrom(ToSocket(m_socket), static_cast<char*>(buffer), static_cast<JBro::Int32>(capacity), 0,
            reinterpret_cast<sockaddr*>(outFrom.data), &fromLength);
        if (received >= 0)
        {
            outReceived = static_cast<std::size_t>(received);
            outFrom.length = static_cast<JBro::UInt32>(fromLength);
            return SocketIo::Ok;
        }
        const Int32 error = WSAGetLastError();
        if (WSAEWOULDBLOCK == error)
        {
            return SocketIo::WouldBlock;
        }
        // ICMP 포트 도달 불가가 WSAECONNRESET 로 올라온다. UDP 에서는 치명이 아니다.
        if (WSAECONNRESET == error)
        {
            return SocketIo::WouldBlock;
        }
        return SocketIo::Error;
    }

    void WinsockDatagramSocket::Close()
    {
        if (InvalidHandle != m_socket)
        {
            closesocket(ToSocket(m_socket));
            m_socket = InvalidHandle;
        }
    }

    Bool WinsockDatagramSocket::IsOpen() const
    {
        return InvalidHandle != m_socket;
    }
}

#endif
