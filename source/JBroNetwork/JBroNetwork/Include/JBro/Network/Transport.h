#pragma once

#include <JBro/Network/Internal/ByteRing.h>
#include <JBro/Network/Internal/WebSocketProtocol.h>
#include <JBro/Network/Socket.h>
#include <JBro/Network/Types.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Network
{
    // 트랜스포트 하나의 고정 예산이다. 전부 `Transport` 생성 때 한 번 잡고 그 뒤로는 할당하지 않는다.
    struct TransportConfig
    {
        std::uint32_t maxConnections = 32;
        // 연결마다: 소켓이 아직 받지 않은 송신 바이트, 프레임이 덜 온 수신 바이트, 조각난 WS 메시지의 조립 버퍼.
        // 앞의 둘은 최대 메시지 하나가 들어갈 만큼으로 올려 잡는다.
        std::uint32_t sendBufferBytes = 64 * 1024;
        std::uint32_t receiveBufferBytes = 64 * 1024;
        // 전체 연결이 공유하는 수신 메시지 저장소. `TakeMessages` 가 여기를 가리키는 뷰를 준다.
        std::uint32_t inboundBytes = 1024 * 1024;
        std::uint32_t inboundMessages = 4096;
        std::uint32_t eventCapacity = 256;
        std::uint32_t maxMessageBytes = 64 * 1024;
        // 세션. hello 로 교환하는 버전은 기본이 `ProtocolVersion` 이고, 테스트가 불일치를 만들 때만 바꾼다.
        std::uint32_t protocolVersion = ProtocolVersion;
        double pingIntervalMilliseconds = 1000.0;
        double timeoutMilliseconds = 5000.0;
    };

    // 연결·WS 프레이밍·세션·큐를 갖는 트랜스포트다(network-plan §2.4). 메인 스레드가 프레임 밖에서 `Update` 를 부르고,
    // 이벤트와 메시지는 콜백이 아니라 `TakeEvents` / `TakeMessages` 로 꺼내 간다. 프로세스 전역 상태가 없으므로
    // 한 프로세스에 몇 개든 둘 수 있다 - 호스트 둘 검증이 그 위에 선다.
    //
    // 와이어는 전 플랫폼 WebSocket(RFC6455) 이고 그 안의 메시지는 [uint16 LE 메시지 ID][페이로드] 다. 채널은 와이어에
    // 실리지 않는다 - 소켓 선택이다. WS 로 온 메시지는 전부 `ReliableOrdered` 로 도착한다(3 단계의 UDP 가 다른 채널을 채운다).
    // `Connected` 는 소켓이 아니라 **hello 로 버전이 맞은 때** 뜬다. 서버가 받아들였다가 hello 전에 떨어진 연결은 게임에 보이지 않는다.
    class Transport final
    {
    public:
        Transport(ISocketProvider& provider, IClock& clock, const TransportConfig& config = {});
        ~Transport();

        Transport(const Transport&) = delete;
        Transport& operator=(const Transport&) = delete;

        // 서버. 이미 역할이 있으면 거짓이다.
        bool Listen(std::uint16_t port);
        // 클라이언트. `host` 는 `ws://` 접두를 벗겨 받는다. 결과는 `Connected` 나 `Disconnected` 이벤트로 온다.
        bool Connect(const char* host, std::uint16_t port);
        // 전부 닫고 역할을 지운다. 준비돼 있던 연결마다 `Disconnected(Normal)` 을 남긴다.
        void Close();
        // 연결 하나를 닫는다. 실제 정리는 이번 `Update` 끝에서 한다 - 순회 중에 지우지 않는다.
        void CloseConnection(ConnectionId connection);

        NetworkRole GetRole() const;
        bool IsListening() const;
        // hello 가 끝나기 전까지는 `Connecting` 이다.
        ConnectionState GetConnectionState(ConnectionId connection) const;
        std::uint32_t GetConnectionCount() const;
        ConnectionId GetConnectionAt(std::uint32_t index) const;
        // 최근 왕복 시간(ms). 아직 재지 않았거나 모르는 연결이면 -1.
        double GetRoundTripMilliseconds(ConnectionId connection) const;

        // 송신 버퍼에 다 들어가지 않으면 거짓이고 아무것도 보내지 않는다(역압). 연결이 준비되지 않았으면 거짓이다.
        // 메시지 ID 는 1..FirstSystemMessageId-1 이다. 0(raw) 은 허용하고 시스템 대역은 거절한다.
        bool Send(
            ConnectionId connection,
            MessageId messageId,
            const void* data,
            std::uint32_t size,
            NetChannel channel = NetChannel::ReliableOrdered);
        // 서버 전용. 하나라도 보냈으면 참이다.
        bool Broadcast(
            MessageId messageId,
            const void* data,
            std::uint32_t size,
            NetChannel channel = NetChannel::ReliableOrdered);

        // 소켓을 폴링해 큐를 채우고 세션(ping·타임아웃)을 돌린다. 매 프레임 한 번. 직전에 꺼내 간 메시지 뷰는 여기서 무효가 된다.
        void Update();

        // 채운 개수를 돌려주고 남은 것은 다음에 이어진다. 큐가 넘쳐 버린 것이 있으면 `Overflow` 하나가 뒤따른다.
        std::uint32_t TakeEvents(NetworkEvent* events, std::uint32_t capacity);
        // 뷰는 다음 `Update` 까지만 유효하다.
        std::uint32_t TakeMessages(MessageView* messages, std::uint32_t capacity);

        const TransportConfig& GetConfig() const;

        // 테스트 전용. 0 보다 크면 데이터 프레임을 이 크기의 조각으로 나눠 보낸다 - 조립 경로를 밟기 위해서다.
        void SetFragmentBytesForTests(std::uint32_t bytes);

    private:
        enum class Phase : std::uint8_t
        {
            // 클라이언트의 논블로킹 접속 진행 중.
            TcpConnecting,
            // WS 오프닝 핸드셰이크 교환 중.
            WebSocketHandshaking,
            // WS 는 열렸고 hello 를 기다린다. 게임에는 아직 알리지 않았다.
            SessionHandshaking,
            // 버전이 맞았다. `Connected` 를 알렸다.
            Ready
        };

        struct Connection
        {
            static constexpr std::uint32_t HostCapacity = 256;

            ConnectionId id = InvalidConnectionId;
            OwnerPtr<IStreamSocket> stream;
            Phase phase = Phase::TcpConnecting;
            bool serverSide = false;
            bool wantsClose = false;
            DisconnectReason closeReason = DisconnectReason::Normal;
            ByteRing send;
            ByteRing receive;
            Array<std::uint8_t> fragment;
            std::uint32_t fragmentSize = 0;
            bool inFragment = false;
            char host[HostCapacity] = {};
            std::uint16_t port = 0;
            char clientKey[WebSocket::ClientKeyChars + 1] = {};
            double lastReceiveMilliseconds = 0.0;
            double lastPingMilliseconds = 0.0;
            double roundTripMilliseconds = -1.0;
        };

        struct InboundRecord
        {
            ConnectionId connection = InvalidConnectionId;
            std::uint32_t offset = 0;
            std::uint32_t size = 0;
            MessageId messageId = RawMessageId;
            NetChannel channel = NetChannel::ReliableOrdered;
        };

        Connection* FindConnection(ConnectionId id);
        const Connection* FindConnection(ConnectionId id) const;
        Connection& AddConnection(ConnectionId id, OwnerPtr<IStreamSocket> stream, bool serverSide);
        void RequestClose(Connection& connection, DisconnectReason reason);
        void PushEvent(NetworkEventKind kind, ConnectionId connection, DisconnectReason reason);

        void CompactInbound();
        void AcceptPending();
        void PollConnection(Connection& connection);
        void BeginWebSocketHandshake(Connection& connection);
        void PumpWebSocketHandshake(Connection& connection);
        void PumpFrames(Connection& connection);
        // 참이면 프레임을 소비했다. 거짓이면 저장소가 없어 멈췄다(프레임은 고리에 남는다).
        bool DeliverDataFrame(Connection& connection, const WebSocket::FrameHeader& header);
        bool DeliverMessage(Connection& connection, const std::uint8_t* message, std::uint32_t size);
        void HandleSystemMessage(Connection& connection, MessageId messageId, const std::uint8_t* payload, std::uint32_t size);
        void PromoteToReady(Connection& connection);
        void UpdateSessions();
        void FlushSend(Connection& connection);
        void ReadIntoRing(Connection& connection);
        std::uint8_t* ReserveInbound(const Connection& connection, MessageId messageId, std::uint32_t size);
        void RemoveClosedConnections();

        bool QueueFrame(Connection& connection, WebSocket::Opcode opcode, bool fin, const std::uint8_t* first,
            std::uint32_t firstSize, const std::uint8_t* second, std::uint32_t secondSize);
        bool QueueMessage(Connection& connection, MessageId messageId, const void* data, std::uint32_t size);
        void WriteMaskedRun(Connection& connection, const std::uint8_t* data, std::uint32_t size, bool mask,
            const std::uint8_t maskBytes[4], std::uint32_t& maskOffset);
        template <typename T>
        void SendSystem(Connection& connection, MessageId messageId, const T& payload);
        std::uint32_t NextMaskKey();

        ISocketProvider& m_provider;
        IClock& m_clock;
        TransportConfig m_config;

        NetworkRole m_role = NetworkRole::None;
        OwnerPtr<IStreamSocket> m_listener;
        Array<Connection> m_connections;
        ConnectionId m_nextClientId = ServerConnectionId + 1;
        std::uint32_t m_maskState = 0x1234ABCDu;
        std::uint32_t m_fragmentBytesForTests = 0;

        Array<std::uint8_t> m_inbound;
        std::uint32_t m_inboundSize = 0;
        Array<InboundRecord> m_records;
        std::uint32_t m_recordCount = 0;
        std::uint32_t m_recordsTaken = 0;

        Array<NetworkEvent> m_events;
        std::uint32_t m_eventHead = 0;
        std::uint32_t m_eventCount = 0;
        bool m_overflowPending = false;

        Array<std::uint8_t> m_scratch;
    };
}
