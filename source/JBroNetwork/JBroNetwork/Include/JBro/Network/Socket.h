#pragma once

#include <JBro/Network/Types.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// 소켓 경계다(network-plan §2.3). 네트워크는 소켓을 직접 열지 않고 `ISocketProvider` 로 받는다.
// 독립 개발 단계에서는 테스트가 인메모리 provider 와 Winsock 을 직접 감싼 provider 를 넘기고,
// 부착 단계에서 `IPlatform` 을 감싼 provider 가 들어온다. `null` 이 "이 플랫폼에는 없다" 다 -
// `#if` 로 계층을 통째로 빼지 않는다.
namespace JBro::Network
{
    // 불투명 엔드포인트. sockaddr_in / in6 를 담을 만큼의 POD 다. 배운 상대 주소로 되돌려 보내는 데 쓴다.
    struct Endpoint
    {
        static constexpr std::uint32_t Capacity = 28;
        std::uint8_t data[Capacity] = {};
        std::uint32_t length = 0;

        bool IsValid() const
        {
            return 0 != length;
        }

        bool Equals(const Endpoint& other) const
        {
            if (length != other.length)
            {
                return false;
            }
            return 0 == std::memcmp(data, other.data, length);
        }
    };

    enum class SocketIo : std::uint8_t
    {
        Ok,
        // 지금은 할 것이 없다. 논블로킹의 정상 상태다.
        WouldBlock,
        // 상대가 닫았다.
        Closed,
        Error
    };

    // 연결 지향 바이트 스트림(TCP). 전부 논블로킹이다. 프레이밍은 위(WS 코덱)가 한다.
    class IStreamSocket
    {
    public:
        virtual ~IStreamSocket() = default;

        // 접속을 시작한다. 결과는 `GetState` 가 `Connecting` 에서 `Connected` 나 `Disconnected` 로 바뀌는 것으로 안다.
        virtual bool Connect(const char* host, std::uint16_t port) = 0;
        virtual bool Listen(std::uint16_t port) = 0;
        // 기다리는 접속이 있으면 그 소켓을, 없으면 null 을 돌려준다. Listen 한 소켓에서만 뜻이 있다.
        virtual OwnerPtr<IStreamSocket> Accept() = 0;
        virtual ConnectionState GetState() const = 0;
        // 보낸 바이트 수를 `outSent` 에 준다. 일부만 보낼 수 있다.
        virtual SocketIo Send(const void* data, std::size_t size, std::size_t& outSent) = 0;
        virtual SocketIo Receive(void* buffer, std::size_t capacity, std::size_t& outReceived) = 0;
        virtual void Close() = 0;
    };

    // 데이터그램 소켓(UDP). 주소 기반이고 연결이 없다.
    class IDatagramSocket
    {
    public:
        virtual ~IDatagramSocket() = default;

        // 소켓을 만들고 논블로킹으로 둔다. 바인드하지 않는다(클라이언트는 첫 SendTo 에서 임시 포트를 받는다).
        virtual bool Open() = 0;
        // 서버가 포트를 잡는다. 0 이면 임시 포트다.
        virtual bool Bind(std::uint16_t port) = 0;
        virtual bool Resolve(const char* host, std::uint16_t port, Endpoint& outEndpoint) = 0;
        virtual SocketIo SendTo(const Endpoint& to, const void* data, std::size_t size) = 0;
        virtual SocketIo ReceiveFrom(void* buffer, std::size_t capacity, std::size_t& outReceived, Endpoint& outFrom) = 0;
        virtual void Close() = 0;
        virtual bool IsOpen() const = 0;
    };

    // WebRTC 피어 연결 하나(network-plan §2.7). 데이터 채널을 `NetChannelCount` 개 열고 채널 열거형과 1:1 로 쓴다.
    // 시그널(SDP·ICE)은 바이트로 꺼내고 넣는다 - 누가 어떻게 중계하는지는 위가 정한다.
    struct PeerConnectionDesc
    {
        // 제안을 만드는 쪽이 참이다.
        bool initiator = false;
        // ICE 서버 목록. `stun:` / `turn:` URL 을 널 문자로 구분해 이어 붙인 UTF-8 이고 마지막은 널 둘이다.
        const char* iceServers = nullptr;
    };

    class IPeerConnection
    {
    public:
        virtual ~IPeerConnection() = default;

        virtual ConnectionState GetState() const = 0;
        // 상대에게 전달할 시그널 바이트를 꺼낸다. 없으면 0.
        virtual std::uint32_t TakeSignal(void* buffer, std::uint32_t capacity) = 0;
        // 상대에게서 받은 시그널 바이트를 넣는다.
        virtual bool PushSignal(const void* data, std::uint32_t size) = 0;
        virtual SocketIo Send(NetChannel channel, const void* data, std::size_t size) = 0;
        virtual SocketIo Receive(NetChannel& outChannel, void* buffer, std::size_t capacity, std::size_t& outReceived) = 0;
        virtual void Close() = 0;
    };

    class ISocketProvider
    {
    public:
        virtual ~ISocketProvider() = default;

        // 없으면 null 이다.
        virtual OwnerPtr<IStreamSocket> CreateStreamSocket() = 0;
        // Web 은 null 이다 - 브라우저에 UDP 가 없다.
        virtual OwnerPtr<IDatagramSocket> CreateDatagramSocket() = 0;
        // WebRTC 스택이 없는 플랫폼은 null 이다.
        virtual OwnerPtr<IPeerConnection> CreatePeerConnection(const PeerConnectionDesc& desc) = 0;
    };

    // 단조 시계. RTT·RTO·keepalive 가 전부 이것을 본다. 주입받으므로 테스트가 시간을 손으로 돌린다.
    class IClock
    {
    public:
        virtual ~IClock() = default;
        virtual double NowMilliseconds() const = 0;
    };

    static_assert(std::is_standard_layout_v<Endpoint>);
    static_assert(std::is_trivially_copyable_v<Endpoint>);
}
