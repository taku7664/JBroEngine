#pragma once

#include <JBro/Network/Internal/ByteRing.h>
#include <JBro/Network/Internal/ReliableEndpoint.h>
#include <JBro/Network/Internal/UdpDatagram.h>
#include <JBro/Network/Internal/UdpPeer.h>
#include <JBro/Network/Internal/WebSocketProtocol.h>
#include <JBro/Network/Socket.h>
#include <JBro/Network/Types.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Network
{
    // 트랜스포트 하나의 고정 예산이다. **역할을 잡을 때**(`Listen`·`Connect`·`HostPeers`·`ConnectPeer`) 한 번 잡고,
    // 그 뒤로는 할당하지 않으며, `Close` 가 돌려준다(UDP 쪽은 연결에 UDP 가 붙을 때). 역할이 없는 트랜스포트는
    // 아무것도 잡지 않는다 - 네트워크를 쓰지 않는 에디터와 단일 플레이가 이 예산을 지지 않게 하기 위해서다.
    struct TransportConfig
    {
        UInt32 maxConnections = 32;
        // 연결마다: 소켓이 아직 받지 않은 송신 바이트, 프레임이 덜 온 수신 바이트, 조각난 WS 메시지의 조립 버퍼.
        // 앞의 둘은 최대 메시지 하나가 들어갈 만큼으로 올려 잡는다.
        UInt32 sendBufferBytes = 64 * 1024;
        UInt32 receiveBufferBytes = 64 * 1024;
        // 전체 연결이 공유하는 수신 메시지 저장소. `TakeMessages` 가 여기를 가리키는 뷰를 준다.
        UInt32 inboundBytes = 1024 * 1024;
        UInt32 inboundMessages = 4096;
        UInt32 eventCapacity = 256;
        UInt32 maxMessageBytes = 64 * 1024;
        // 세션. hello 로 교환하는 버전은 기본이 `ProtocolVersion` 이고, 테스트가 불일치를 만들 때만 바꾼다.
        UInt32 protocolVersion = ProtocolVersion;
        double pingIntervalMilliseconds = 1000.0;
        double timeoutMilliseconds = 5000.0;
        // UDP. 거짓이면 데이터그램 소켓을 열지 않는다 - 전 채널이 WS 다. 플랫폼에 UDP 가 없으면 참이어도 그렇게 된다.
        Bool udpEnabled = true;
        // 준비되면 UDP, 이 시간 안에 못 오면 WS 로 사용자 `ReliableOrdered` 전송로를 확정한다.
        double orderedRouteWaitMilliseconds = 2000.0;
        UInt32 orderedBacklogBytes = 64 * 1024;
        // 클라이언트가 서버의 답을 받기 전까지 punch 를 다시 보내는 간격.
        double punchRetryMilliseconds = 100.0;
        ReliableConfig reliable;
    };

    // 신뢰 UDP 엔진의 진단값. 테스트와 디버그 표시가 본다.
    struct ReliableDiagnostics
    {
        Bool udpReady = false;
        OrderedRoute route = OrderedRoute::Undecided;
        UInt32 unacked = 0;
        UInt32 queued = 0;
        UInt32 congestionWindow = 0;
        UInt32 piggybackAcks = 0;
        UInt32 standaloneAcks = 0;
        double rtoMilliseconds = 0.0;
        double smoothedRttMilliseconds = 0.0;
    };

    // 연결·WS 프레이밍·세션·UDP 채널·큐를 갖는 트랜스포트다(network-plan §2.4·§2.5). 메인 스레드가 프레임 밖에서 `Update` 를
    // 부르고, 이벤트와 메시지는 콜백이 아니라 `TakeEvents` / `TakeMessages` 로 꺼내 간다. 프로세스 전역 상태가 없으므로
    // 한 프로세스에 몇 개든 둘 수 있다 - 호스트 둘 검증이 그 위에 선다.
    //
    // 와이어는 전 플랫폼 WebSocket(RFC6455) 기준선이고 그 안의 메시지는 [uint16 LE 메시지 ID][페이로드] 다. 네이티브끼리는
    // 같은 포트의 UDP 를 덧대어 비신뢰 채널을 UDP 로, 신뢰 채널을 UDP-신뢰(재전송·ACK·재정렬·조각·AIMD)로 승격한다.
    // 채널은 소켓 선택이지 와이어 페이로드가 아니다. UDP 가 없으면(웹·방화벽) 전부 WS 로 투명하게 폴백한다.
    // `Connected` 는 소켓이 아니라 **hello 로 버전이 맞은 때** 뜬다. 서버가 받아들였다가 hello 전에 떨어진 연결은 게임에 보이지 않는다.
    class Transport final
    {
    public:
        Transport(ISocketProvider& provider, IClock& clock, const TransportConfig& config = {});
        ~Transport();

        Transport(const Transport&) = delete;
        Transport& operator=(const Transport&) = delete;

        // 서버. WS 와 같은 번호의 UDP 포트도 잡는다(못 잡으면 UDP 없이 간다). 이미 역할이 있으면 거짓이다.
        Bool Listen(std::uint16_t port);
        // 클라이언트. `host` 는 `ws://` 접두를 벗겨 받는다. 결과는 `Connected` 나 `Disconnected` 이벤트로 온다.
        Bool Connect(const char* host, std::uint16_t port);
        // 전부 닫고 역할을 지운다. 준비돼 있던 연결마다 `Disconnected(Normal)` 을 남긴다.
        void Close();
        // 연결 하나를 닫는다. 실제 정리는 이번 `Update` 끝에서 한다 - 순회 중에 지우지 않는다.
        void CloseConnection(ConnectionId connection);

        // ── 피어형 연결(WebRTC, network-plan §2.7) ──
        // 소켓을 듣는 대신 피어를 받는 서버가 된다. 웹 호스트가 이것이다. 이미 역할이 있으면 거짓이다.
        Bool HostPeers();
        // 호스트: 상대의 제안을 기다리는 피어 하나를 만든다. 시그널은 `TakePeerSignal` / `PushPeerSignal` 로 호출측이 중계한다.
        // 플랫폼에 WebRTC 가 없으면 `InvalidConnectionId`.
        ConnectionId AcceptPeer();
        // 클라이언트: 제안을 만드는 피어 하나로 호스트에 붙는다(`ServerConnectionId`).
        Bool ConnectPeer();
        // 상대에게 전할 시그널 바이트. 없으면 0. 시그널이 어떻게 건너가는지는 트랜스포트가 모른다 - 시그널링 서버의 일이다.
        UInt32 TakePeerSignal(ConnectionId connection, void* buffer, UInt32 capacity);
        Bool PushPeerSignal(ConnectionId connection, const void* data, UInt32 size);
        ConnectionKind GetConnectionKind(ConnectionId connection) const;

        NetworkRole GetRole() const;
        Bool IsListening() const;
        // hello 가 끝나기 전까지는 `Connecting` 이다.
        ConnectionState GetConnectionState(ConnectionId connection) const;
        UInt32 GetConnectionCount() const;
        ConnectionId GetConnectionAt(UInt32 index) const;
        // 최근 왕복 시간(ms). 아직 재지 않았거나 모르는 연결이면 -1.
        double GetRoundTripMilliseconds(ConnectionId connection) const;
        // 비신뢰 채널의 수신 손실률(0~1). 표본이 없거나 UDP 가 없으면 -1. 게임이 송신 빈도와 보간 창을 고르는 데 쓴다.
        double GetUdpLossRate(ConnectionId connection) const;
        Bool GetReliableDiagnostics(ConnectionId connection, ReliableDiagnostics& out) const;

        // 송신 버퍼에 다 들어가지 않으면 거짓이고 아무것도 보내지 않는다(역압). 연결이 준비되지 않았으면 거짓이다.
        // 메시지 ID 는 1..FirstSystemMessageId-1 이다. 0(raw) 은 허용하고 시스템 대역은 거절한다.
        Bool Send(
            ConnectionId connection,
            MessageId messageId,
            const void* data,
            UInt32 size,
            NetChannel channel = NetChannel::ReliableOrdered);
        // 서버 전용. 하나라도 보냈으면 참이다.
        Bool Broadcast(
            MessageId messageId,
            const void* data,
            UInt32 size,
            NetChannel channel = NetChannel::ReliableOrdered);

        // 소켓을 폴링해 큐를 채우고 세션(ping·타임아웃)과 UDP(재전송·ack·전송로 확정)를 돌린다. 매 프레임 한 번.
        // 직전에 꺼내 간 메시지 뷰는 여기서 무효가 된다.
        void Update();

        // 채운 개수를 돌려주고 남은 것은 다음에 이어진다. 큐가 넘쳐 버린 것이 있으면 `Overflow` 하나가 뒤따른다.
        UInt32 TakeEvents(NetworkEvent* events, UInt32 capacity);
        // 뷰는 다음 `Update` 까지만 유효하다. `channel` 은 실제로 도착한 채널이다(WS 는 전부 `ReliableOrdered`).
        UInt32 TakeMessages(MessageView* messages, UInt32 capacity);

        const TransportConfig& GetConfig() const;
        // 복제가 스냅숏 간격을 재는 데 쓴다. 트랜스포트가 소유하지 않는 시계다.
        IClock& GetClock() const;
        // 지금 잡고 있는 공용 버퍼의 바이트. 역할이 없으면 0 이다(연결마다 따로 잡는 것은 세지 않는다).
        UInt32 GetReservedBytes() const;

        // 테스트 전용. 0 보다 크면 WS 데이터 프레임을 이 크기의 조각으로 나눠 보낸다 - 조립 경로를 밟기 위해서다.
        void SetFragmentBytesForTests(UInt32 bytes);

    private:
        enum class Phase : std::uint8_t
        {
            // 클라이언트의 논블로킹 접속 진행 중.
            TcpConnecting,
            // 피어형. 시그널이 오가고 데이터 채널이 열리기를 기다린다.
            PeerConnecting,
            // WS 오프닝 핸드셰이크 교환 중.
            WebSocketHandshaking,
            // WS 는 열렸고 hello 를 기다린다. 게임에는 아직 알리지 않았다.
            SessionHandshaking,
            // 버전이 맞았다. `Connected` 를 알렸다.
            Ready
        };

        struct Connection
        {
            static constexpr UInt32 HostCapacity = 256;

            ConnectionId id = InvalidConnectionId;
            OwnerPtr<IStreamSocket> stream;
            // 피어형이면 이것이 있고 `stream` 은 비어 있다. 채널은 데이터 채널이 스스로 지키므로 UDP 도 신뢰 엔진도 쓰지 않는다.
            OwnerPtr<IPeerConnection> peer;
            ConnectionKind kind = ConnectionKind::Socket;
            Phase phase = Phase::TcpConnecting;
            Bool serverSide = false;
            Bool wantsClose = false;
            DisconnectReason closeReason = DisconnectReason::Normal;
            ByteRing send;
            ByteRing receive;
            Array<std::uint8_t> fragment;
            UInt32 fragmentSize = 0;
            Bool inFragment = false;
            char host[HostCapacity] = {};
            std::uint16_t port = 0;
            char clientKey[WebSocket::ClientKeyChars + 1] = {};
            double lastReceiveMilliseconds = 0.0;
            double lastPingMilliseconds = 0.0;
            double roundTripMilliseconds = -1.0;
            UdpPeer udp;
        };

        struct InboundRecord
        {
            ConnectionId connection = InvalidConnectionId;
            UInt32 offset = 0;
            UInt32 size = 0;
            MessageId messageId = RawMessageId;
            NetChannel channel = NetChannel::ReliableOrdered;
        };

        // 신뢰 엔진이 내보내는 데이터그램에 토큰을 찍어 소켓으로 보낸다.
        class PeerEmitter final : public IDatagramEmitter
        {
        public:
            PeerEmitter(Transport& transport, const Endpoint& to, UInt64 token);
            void Emit(UdpProto::DatagramHeader& header, const std::uint8_t* payload, UInt32 size) override;

        private:
            Transport& m_transport;
            Endpoint m_to;
            UInt64 m_token;
        };

        // 신뢰 엔진이 올린 메시지를 수신 저장소에 넣는다.
        class InboundReceiver final : public IReliableReceiver
        {
        public:
            InboundReceiver(Transport& transport, Connection& connection);
            void Deliver(NetChannel channel, MessageId messageId, const std::uint8_t* payload, UInt32 size) override;

        private:
            Transport& m_transport;
            Connection& m_connection;
        };

        Connection* FindConnection(ConnectionId id);
        const Connection* FindConnection(ConnectionId id) const;
        Connection* FindConnectionByToken(UInt64 token);
        Bool SendTo(Connection& target, MessageId messageId, const void* data, UInt32 size, NetChannel channel);
        Connection& AddConnection(ConnectionId id, OwnerPtr<IStreamSocket> stream, Bool serverSide);
        Connection& AddPeerConnection(ConnectionId id, OwnerPtr<IPeerConnection> peer, Bool serverSide);
        void PollPeer(Connection& connection);
        Bool SendOverPeer(Connection& connection, MessageId messageId, const void* data, UInt32 size, NetChannel channel);
        // 시스템 메시지를 연결 종류에 맞는 길로 보낸다.
        void SendControl(Connection& connection, MessageId messageId, const void* data, UInt32 size);
        void RequestClose(Connection& connection, DisconnectReason reason);
        void PushEvent(NetworkEventKind kind, ConnectionId connection, DisconnectReason reason);

        // 역할을 잡을 때 공용 버퍼를 한 번 잡고, `Close` 에서 돌려준다. 이벤트 큐만은 `Close` 가 남긴
        // `Disconnected` 를 게임이 아직 꺼내 가지 않았으므로 소멸자까지 산다.
        void EnsureBuffers();
        void ReleaseBuffers();
        void CompactInbound();
        void AcceptPending();
        void PollConnection(Connection& connection);
        void BeginWebSocketHandshake(Connection& connection);
        void PumpWebSocketHandshake(Connection& connection);
        void PumpFrames(Connection& connection);
        // 참이면 프레임을 소비했다. 거짓이면 저장소가 없어 멈췼다(프레임은 고리에 남는다).
        Bool DeliverDataFrame(Connection& connection, const WebSocket::FrameHeader& header);
        Bool DeliverMessage(Connection& connection, const std::uint8_t* message, UInt32 size);
        void HandleSystemMessage(Connection& connection, MessageId messageId, const std::uint8_t* payload, UInt32 size);
        void PromoteToReady(Connection& connection);
        void UpdateSessions();
        void FlushSend(Connection& connection);
        void ReadIntoRing(Connection& connection);
        std::uint8_t* ReserveInbound(const Connection& connection, MessageId messageId, NetChannel channel, UInt32 size);
        void StoreUdpMessage(Connection& connection, NetChannel channel, MessageId messageId, const std::uint8_t* payload,
            UInt32 size);
        void RemoveClosedConnections();

        Bool QueueFrame(Connection& connection, WebSocket::Opcode opcode, Bool fin, const std::uint8_t* first,
            UInt32 firstSize, const std::uint8_t* second, UInt32 secondSize);
        Bool QueueMessage(Connection& connection, MessageId messageId, const void* data, UInt32 size);
        Bool SendOverWebSocket(Connection& connection, MessageId messageId, const void* data, UInt32 size);
        void WriteMaskedRun(Connection& connection, const std::uint8_t* data, UInt32 size, Bool mask,
            const std::uint8_t maskBytes[4], UInt32& maskOffset);
        template <typename T>
        void SendSystem(Connection& connection, MessageId messageId, const T& payload);
        UInt32 NextMaskKey();

        // ── UDP ──
        void OpenServerUdp(std::uint16_t port);
        void AttachClientUdp(Connection& connection, UInt64 token);
        void AttachUdpPeer(Connection& connection);
        Bool SendUdpDatagram(Connection& connection, NetChannel channel, MessageId messageId, const void* data, UInt32 size);
        Bool SendUdpReliable(Connection& connection, NetChannel channel, MessageId messageId, const void* data, UInt32 size);
        Bool SendPacket(const Endpoint& to, UdpProto::DatagramHeader& header, const std::uint8_t* payload, UInt32 size);
        void SendPunch(Connection& connection);
        Bool SendUserOrdered(Connection& connection, MessageId messageId, const void* data, UInt32 size);
        Bool PushBacklog(Connection& connection, MessageId messageId, const void* data, UInt32 size);
        void FlushBacklog(Connection& connection);
        void CommitOrderedRoutes();
        void PollUdp();
        void HandleDatagram(Connection& connection, const UdpProto::DatagramHeader& header, const std::uint8_t* payload,
            UInt32 size);
        void TickUdpPeers();
        UInt64 NextToken();
        Bool InboundHasHeadroom() const;
        // UDP 가 이 트랜스포트에 올 수 있는가. 서버는 소켓이 있어야 하고, 클라이언트는 토큰이 오거나 기다림이 끝나기 전까지 모른다.
        Bool UdpPossible() const;

        ISocketProvider& m_provider;
        IClock& m_clock;
        TransportConfig m_config;

        NetworkRole m_role = NetworkRole::None;
        // 소켓을 듣지 않고 피어를 받는 서버다.
        Bool m_peerHosting = false;
        OwnerPtr<IStreamSocket> m_listener;
        OwnerPtr<IDatagramSocket> m_udpSocket;
        // 클라이언트가 UDP 를 켜 보았으나 이 플랫폼에 없었다. 그 뒤로는 기다리지 않고 WS 다.
        Bool m_udpUnavailable = false;
        Array<Connection> m_connections;
        ConnectionId m_nextClientId = ServerConnectionId + 1;
        UInt32 m_maskState = 0x1234ABCDu;
        UInt64 m_tokenState = 0xD1B54A32D192ED03ull;
        UInt32 m_fragmentBytesForTests = 0;

        Array<std::uint8_t> m_inbound;
        UInt32 m_inboundSize = 0;
        Array<InboundRecord> m_records;
        UInt32 m_recordCount = 0;
        UInt32 m_recordsTaken = 0;

        Array<NetworkEvent> m_events;
        UInt32 m_eventHead = 0;
        UInt32 m_eventCount = 0;
        Bool m_overflowPending = false;

        Array<std::uint8_t> m_scratch;
        Array<std::uint8_t> m_datagramScratch;
        // 백로그에서 꺼낸 메시지 하나를 담는다. 최대 메시지 크기다.
        Array<std::uint8_t> m_messageScratch;
    };
}
