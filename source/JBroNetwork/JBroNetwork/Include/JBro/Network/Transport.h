#pragma once

#include <JBro/Network/Internal/ByteRing.h>
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
        // 연결마다: 소켓이 아직 받지 않은 송신 바이트, 프레임이 덜 온 수신 바이트.
        std::uint32_t sendBufferBytes = 64 * 1024;
        std::uint32_t receiveBufferBytes = 64 * 1024;
        // 전체 연결이 공유하는 수신 메시지 저장소. `TakeMessages` 가 여기를 가리키는 뷰를 준다.
        std::uint32_t inboundBytes = 1024 * 1024;
        std::uint32_t inboundMessages = 4096;
        std::uint32_t eventCapacity = 256;
        std::uint32_t maxMessageBytes = 64 * 1024;
    };

    // 연결·프레이밍·큐를 갖는 트랜스포트다(network-plan §2.4). 메인 스레드가 프레임 밖에서 `Update` 를 부르고,
    // 이벤트와 메시지는 콜백이 아니라 `TakeEvents` / `TakeMessages` 로 꺼내 간다. 프로세스 전역 상태가 없으므로
    // 한 프로세스에 몇 개든 둘 수 있다 - 호스트 둘 검증이 그 위에 선다.
    //
    // 1 단계는 길이 접두 프레이밍이다. 2 단계가 이것을 WebSocket 프레임과 세션(hello·버전·keepalive)으로 바꾼다.
    class Transport final
    {
    public:
        Transport(ISocketProvider& provider, IClock& clock, const TransportConfig& config = {});
        ~Transport();

        Transport(const Transport&) = delete;
        Transport& operator=(const Transport&) = delete;

        // 서버. 이미 역할이 있으면 거짓이다.
        bool Listen(std::uint16_t port);
        // 클라이언트. 결과는 `Connected` 나 `Disconnected` 이벤트로 온다.
        bool Connect(const char* host, std::uint16_t port);
        // 전부 닫고 역할을 지운다. 열려 있던 연결마다 `Disconnected(Normal)` 을 남긴다.
        void Close();
        // 연결 하나를 닫는다. 실제 정리는 이번 `Update` 끝에서 한다 - 순회 중에 지우지 않는다.
        void CloseConnection(ConnectionId connection);

        NetworkRole GetRole() const;
        bool IsListening() const;
        ConnectionState GetConnectionState(ConnectionId connection) const;
        std::uint32_t GetConnectionCount() const;
        ConnectionId GetConnectionAt(std::uint32_t index) const;

        // 송신 버퍼에 다 들어가지 않으면 거짓이고 아무것도 보내지 않는다(역압). 연결이 준비되지 않았으면 거짓이다.
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

        // 소켓을 폴링해 큐를 채운다. 매 프레임 한 번. 직전에 꺼내 간 메시지 뷰는 여기서 무효가 된다.
        void Update();

        // 채운 개수를 돌려주고 남은 것은 다음에 이어진다. 큐가 넘쳐 버린 것이 있으면 `Overflow` 하나가 뒤따른다.
        std::uint32_t TakeEvents(NetworkEvent* events, std::uint32_t capacity);
        // 뷰는 다음 `Update` 까지만 유효하다.
        std::uint32_t TakeMessages(MessageView* messages, std::uint32_t capacity);

        const TransportConfig& GetConfig() const;

    private:
        struct Connection
        {
            ConnectionId id = InvalidConnectionId;
            OwnerPtr<IStreamSocket> stream;
            ConnectionState state = ConnectionState::Disconnected;
            bool wantsClose = false;
            DisconnectReason closeReason = DisconnectReason::Normal;
            ByteRing send;
            ByteRing receive;
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
        Connection& AddConnection(ConnectionId id, OwnerPtr<IStreamSocket> stream, ConnectionState state);
        void RequestClose(Connection& connection, DisconnectReason reason);
        void PushEvent(NetworkEventKind kind, ConnectionId connection, DisconnectReason reason);

        void CompactInbound();
        void AcceptPending();
        void PollConnection(Connection& connection);
        void FlushSend(Connection& connection);
        void ReadIntoRing(Connection& connection);
        void ParseFrames(Connection& connection);
        bool StoreInbound(const Connection& connection, MessageId messageId, NetChannel channel, std::uint32_t size);
        void RemoveClosedConnections();

        ISocketProvider& m_provider;
        IClock& m_clock;
        TransportConfig m_config;

        NetworkRole m_role = NetworkRole::None;
        OwnerPtr<IStreamSocket> m_listener;
        Array<Connection> m_connections;
        ConnectionId m_nextClientId = ServerConnectionId + 1;

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
