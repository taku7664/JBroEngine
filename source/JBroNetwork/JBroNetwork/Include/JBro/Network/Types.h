#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// 네트워크의 공개 값 타입이다(network-plan §2.1 Types). 기존 엔진 `NetworkTypes.h` 의 열거형과 상수를 접두어만 떼고 잇는다.
// 여기 있는 것은 전부 POD 다 - 스크립트 DLL 경계와 와이어를 그대로 건넌다.
namespace JBro::Network
{
    // ── 연결 식별 ──────────────────────────────────────────────────────────────────────────────

    using ConnectionId = std::uint64_t;
    inline constexpr ConnectionId InvalidConnectionId = 0;
    // 클라이언트는 자기가 붙은 서버를 이 번호로 가리킨다. 서버가 클라이언트에 주는 번호는 2 부터다.
    inline constexpr ConnectionId ServerConnectionId = 1;

    // 세션 와이어 프로토콜 버전. 와이어(hello·프레이밍·데이터그램 헤더)가 바뀌면 올린다.
    // hello 때 양쪽이 교환·검증하고, 다르면 `VersionMismatch` 로 즉시 거부한다.
    inline constexpr std::uint32_t ProtocolVersion = 1;

    // ── 열거 ───────────────────────────────────────────────────────────────────────────────────

    enum class NetworkRole : std::uint8_t
    {
        None,
        Client,
        Server
    };

    enum class ConnectionState : std::uint8_t
    {
        Disconnected,
        Connecting,
        Connected
    };

    // `Disconnected` 이벤트에 딸려 오는 끊김 사유다.
    enum class DisconnectReason : std::uint8_t
    {
        // 상대가 정상 종료했거나 로컬이 닫았다.
        Normal,
        // keepalive 무응답. 연결이 조용히 죽었다(TCP 반쪽 열림 등).
        Timeout,
        // hello 의 프로토콜 버전이 달라 거부했다.
        VersionMismatch,
        // 전송·프로토콜 오류.
        Error
    };

    // 메시지의 전달 보장. 게임이 메시지마다 고른다(기본 `ReliableOrdered`). 채널은 소켓 선택이지 와이어 페이로드가 아니다.
    // 웹은 UDP 가 없어 전 채널이 신뢰 WS 로 가고, 네이티브는 UDP 가 준비되면 비신뢰 채널이 UDP 로, 신뢰 채널이 UDP-신뢰로 승격된다.
    enum class NetChannel : std::uint8_t
    {
        // 순서 보장 신뢰. 상태 변경·중요 사건. 기본값.
        ReliableOrdered,
        // 신뢰 전달, 순서 무관. HOL 회피가 필요한 최저 지연 사건(히트 판정).
        ReliableUnordered,
        // 유실 허용. 최신 값만 중요한 것(위치).
        Unreliable,
        // 유실 허용 + (연결, 메시지 ID)별 순서 역전 폐기.
        UnreliableSequenced
    };
    inline constexpr std::uint32_t NetChannelCount = 4;

    // 연결이 어떻게 맺어졌는가. 성립한 뒤에는 상위가 둘을 구분하지 않는다(network-plan §2.4).
    enum class ConnectionKind : std::uint8_t
    {
        // Connect / Listen. WebSocket 과 그 위 UDP.
        Socket,
        // 시그널 채널로 SDP·ICE 를 교환해 이은 WebRTC 피어.
        Peer
    };

    // ── 메시지 ─────────────────────────────────────────────────────────────────────────────────

    // 메시지 ID 는 게임이 숫자로 명시한다. 윈 서버와 웹 클라가 다른 컴파일러로 빌드되므로 타입 태그 주소를 와이어에 쓸 수 없다.
    using MessageId = std::uint16_t;
    // 0 은 raw 경로 예약이다.
    inline constexpr MessageId RawMessageId = 0;
    // 여기부터는 시스템 메시지(hello·ping·pong·bye·udp-token). 게임은 1..FirstSystemMessageId-1 을 쓴다.
    inline constexpr MessageId FirstSystemMessageId = 0xFF00;

    // 꺼내 간 수신 메시지 하나다. `data` 는 트랜스포트 안의 수신 버퍼를 가리키고 **다음 `Update` 까지만** 유효하다 -
    // `IPlatform::GetInputEvents` 와 같은 규약이다. 오래 들고 있을 것은 복사한다.
    struct MessageView
    {
        ConnectionId connection = InvalidConnectionId;
        const std::uint8_t* data = nullptr;
        std::uint32_t size = 0;
        MessageId messageId = RawMessageId;
        NetChannel channel = NetChannel::ReliableOrdered;
    };

    // ── 이벤트 ─────────────────────────────────────────────────────────────────────────────────

    enum class NetworkEventKind : std::uint8_t
    {
        // 세션이 준비됐다 - 소켓이 붙은 때가 아니라 hello 로 버전이 맞은 때다.
        Connected,
        // `reason` 이 왜인지 말한다.
        Disconnected,
        // 이벤트 큐나 메시지 큐가 차서 무엇을 버렸다. 받는 쪽은 상태를 다시 조회해야 한다.
        Overflow
    };

    struct NetworkEvent
    {
        NetworkEventKind kind = NetworkEventKind::Overflow;
        DisconnectReason reason = DisconnectReason::Normal;
        ConnectionId connection = InvalidConnectionId;
    };

    static_assert(std::is_standard_layout_v<MessageView>);
    static_assert(std::is_trivially_copyable_v<MessageView>);
    static_assert(std::is_standard_layout_v<NetworkEvent>);
    static_assert(std::is_trivially_copyable_v<NetworkEvent>);
}
