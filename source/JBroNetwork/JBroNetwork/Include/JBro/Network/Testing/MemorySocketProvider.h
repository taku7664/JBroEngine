#pragma once

#include <JBro/Network/Internal/ByteRing.h>
#include <JBro/Network/Socket.h>
#include <JBro/Network/Testing/LossyDatagramSocket.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Network::Testing
{
    class MemoryStreamSocket;
    class MemoryDatagramSocket;
    class MemoryPeerConnection;

    // 한 프로세스 안의 가짜 네트워크다. 스트림은 파이프 한 쌍, 데이터그램은 포트 표다. 소켓은 이 provider 보다
    // 오래 살면 안 된다 - 테스트가 provider 를 먼저 만들고 마지막에 없앤다.
    // 접속은 즉시 붙지 않고 상대가 `Accept` 할 때까지 `Connecting` 이다 - 실제 소켓의 상태 전이를 흉내 내기 위해서다.
    class MemorySocketProvider final : public ISocketProvider
    {
    public:
        struct Pipe
        {
            ByteRing toAcceptor;
            ByteRing toConnector;
            bool connectorOpen = true;
            bool acceptorOpen = true;
            bool accepted = false;
        };

        static constexpr std::uint32_t MaxDatagramBytes = 1500;

        struct Datagram
        {
            Endpoint from;
            std::uint32_t size = 0;
            std::uint8_t data[MaxDatagramBytes] = {};
        };

        // 피어 한 쌍의 데이터 채널 넷, 양방향. 레코드는 [uint32 size][bytes] 다 - 데이터 채널은 메시지 경계를 지킨다.
        struct PeerLink
        {
            ByteRing toAcceptor[NetChannelCount];
            ByteRing toInitiator[NetChannelCount];
            bool initiatorOpen = true;
            bool acceptorOpen = true;
            // 응답 시그널이 제안자에게 닿은 뒤에야 양쪽이 Connected 다 - 실제 흐름과 같은 순서다.
            bool linked = false;
            std::uint32_t rng = 0x9E3779B9u;
        };

        explicit MemorySocketProvider(std::uint32_t pipeBytes = 256 * 1024, std::uint32_t datagramQueueLength = 256);
        ~MemorySocketProvider() override;

        OwnerPtr<IStreamSocket> CreateStreamSocket() override;
        OwnerPtr<IDatagramSocket> CreateDatagramSocket() override;
        // 인메모리 WebRTC 흉내. 시그널은 "O<번호>"(제안) 와 "A<번호>"(응답) 글자다. 거짓으로 두면 null 이다 - 스택이 없는 플랫폼.
        OwnerPtr<IPeerConnection> CreatePeerConnection(const PeerConnectionDesc& desc) override;
        void SetPeerAvailable(bool available);

        // 거짓이면 `CreateDatagramSocket` 이 null 이다 - UDP 가 없는 플랫폼(웹)을 흉내 낸다.
        void SetDatagramAvailable(bool available);
        // 이 뒤로 만드는 데이터그램 소켓을 유실 데코레이터로 감싼다. 만든 순서대로 `GetLossySocketAt` 으로 본다.
        void SetLossy(const LossyConfig& config);
        void ClearLossy();
        LossyDatagramSocket* GetLossySocketAt(std::uint32_t index) const;

        // ── 소켓이 쓰는 내부 API ──
        bool RegisterListener(std::uint16_t port);
        void UnregisterListener(std::uint16_t port);
        Pipe* ConnectTo(std::uint16_t port);
        Pipe* TakePendingConnection(std::uint16_t port);
        void ReleasePipeSide(Pipe* pipe, bool connector);

        bool BindDatagram(std::uint16_t& port, MemoryDatagramSocket* socket);
        void UnbindDatagram(std::uint16_t port);
        bool DeliverDatagram(const Endpoint& to, const Endpoint& from, const void* data, std::size_t size);

        // ── 피어가 쓰는 내부 API ──
        PeerLink* LinkPeers(std::uint32_t initiatorIndex, MemoryPeerConnection* acceptor);
        void UnregisterPeer(MemoryPeerConnection* peer);
        bool ShouldDropUnreliable(PeerLink& link);

        static Endpoint MakeEndpoint(std::uint16_t port);
        static std::uint16_t PortOf(const Endpoint& endpoint);

        std::uint32_t GetDatagramQueueLength() const;

    private:
        struct Listener
        {
            std::uint16_t port = 0;
            Array<Pipe*> pending;
        };

        struct DatagramBinding
        {
            std::uint16_t port = 0;
            MemoryDatagramSocket* socket = nullptr;
        };

        void SweepPipes();

        std::uint32_t m_pipeBytes;
        std::uint32_t m_datagramQueueLength;
        bool m_datagramAvailable = true;
        bool m_lossy = false;
        LossyConfig m_lossyConfig;
        // 소켓은 트랜스포트가 소유한다. 여기 것은 통계를 보기 위한 비소유 포인터이고 소켓이 죽으면 쓰지 않는다.
        Array<LossyDatagramSocket*> m_lossySockets;
        Array<OwnerPtr<Pipe>> m_pipes;
        Array<Listener> m_listeners;
        Array<DatagramBinding> m_datagramBindings;
        std::uint16_t m_nextEphemeralPort = 49152;
        bool m_peerAvailable = true;
        Array<OwnerPtr<PeerLink>> m_peerLinks;
        // 번호 → 피어. 죽은 피어는 null 이다.
        Array<MemoryPeerConnection*> m_peers;
    };

    class MemoryStreamSocket final : public IStreamSocket
    {
    public:
        explicit MemoryStreamSocket(MemorySocketProvider& provider);
        MemoryStreamSocket(MemorySocketProvider& provider, MemorySocketProvider::Pipe* pipe, bool connector);
        ~MemoryStreamSocket() override;

        bool Connect(const char* host, std::uint16_t port) override;
        bool Listen(std::uint16_t port) override;
        OwnerPtr<IStreamSocket> Accept() override;
        ConnectionState GetState() const override;
        SocketIo Send(const void* data, std::size_t size, std::size_t& outSent) override;
        SocketIo Receive(void* buffer, std::size_t capacity, std::size_t& outReceived) override;
        void Close() override;

    private:
        ByteRing& Outgoing() const;
        ByteRing& Incoming() const;
        bool PeerOpen() const;

        MemorySocketProvider& m_provider;
        MemorySocketProvider::Pipe* m_pipe = nullptr;
        bool m_connector = false;
        bool m_listening = false;
        bool m_closed = false;
        std::uint16_t m_port = 0;
    };

    class MemoryDatagramSocket final : public IDatagramSocket
    {
    public:
        explicit MemoryDatagramSocket(MemorySocketProvider& provider);
        ~MemoryDatagramSocket() override;

        bool Open() override;
        bool Bind(std::uint16_t port) override;
        bool Resolve(const char* host, std::uint16_t port, Endpoint& outEndpoint) override;
        SocketIo SendTo(const Endpoint& to, const void* data, std::size_t size) override;
        SocketIo ReceiveFrom(void* buffer, std::size_t capacity, std::size_t& outReceived, Endpoint& outFrom) override;
        void Close() override;
        bool IsOpen() const override;

        // provider 가 배달할 때 부른다. 꽉 차면 버린다 - UDP 다.
        bool Enqueue(const Endpoint& from, const void* data, std::size_t size);

    private:
        MemorySocketProvider& m_provider;
        Array<MemorySocketProvider::Datagram> m_queue;
        std::uint32_t m_head = 0;
        std::uint32_t m_count = 0;
        bool m_open = false;
        bool m_bound = false;
        std::uint16_t m_port = 0;
    };
}

namespace JBro::Network::Testing
{
    // 인메모리 피어 연결. 제안자가 "O<번호>" 를 내고, 수락자가 그것을 받아 링크를 만들고 "A<번호>" 를 내고, 제안자가 그것을 받으면 양쪽이 열린다.
    class MemoryPeerConnection final : public IPeerConnection
    {
    public:
        MemoryPeerConnection(MemorySocketProvider& provider, std::uint32_t index, bool initiator);
        ~MemoryPeerConnection() override;

        ConnectionState GetState() const override;
        std::uint32_t TakeSignal(void* buffer, std::uint32_t capacity) override;
        bool PushSignal(const void* data, std::uint32_t size) override;
        SocketIo Send(NetChannel channel, const void* data, std::size_t size) override;
        SocketIo Receive(NetChannel& outChannel, void* buffer, std::size_t capacity, std::size_t& outReceived) override;
        void Close() override;

        void AttachLink(MemorySocketProvider::PeerLink* link);
        std::uint32_t Index() const;

    private:
        enum class Stage : std::uint8_t
        {
            Idle,
            WaitingAnswer,
            AnswerReady,
            Done
        };

        bool PeerOpen() const;
        ByteRing& Outgoing(NetChannel channel) const;
        ByteRing& Incoming(NetChannel channel) const;

        MemorySocketProvider& m_provider;
        std::uint32_t m_index;
        bool m_initiator;
        MemorySocketProvider::PeerLink* m_link = nullptr;
        Stage m_stage = Stage::Idle;
        bool m_closed = false;
    };
}
