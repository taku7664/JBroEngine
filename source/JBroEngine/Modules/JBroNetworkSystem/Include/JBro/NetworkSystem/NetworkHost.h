#pragma once

#include <JBro/Network/Internal/SystemContext.h>
#include <JBro/Network/Replication/IReplicatedPool.h>
#include <JBro/Network/Replication/ReplicationClient.h>
#include <JBro/Network/Replication/ReplicationServer.h>
#include <JBro/Network/ServiceContext.h>
#include <JBro/Network/Socket.h>
#include <JBro/Network/System/INetworkSystem.h>
#include <JBro/Network/Transport.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstdint>

namespace JBro
{
    class Canvas;

    // 호스트가 소유하는 네트워크다(network-plan §2.4·§2.8). 트랜스포트는 캔버스보다 오래 산다 - 로비에서 전장으로 캔버스를
    // 갈아 끼워도 연결은 남는다. 캔버스는 프레임워크가 `BindCanvas` 로 묶고 `UnbindCanvas` 로 푼다. 그때마다 복제가 새로 선다.
    //
    // **연결하기 전까지 아무것도 잡지 않는다.** 엔진은 이 객체를 늘 세우지만(스크립트가 서비스로 켤 수 있어야 하므로),
    // 트랜스포트 버퍼와 복제는 게임이 `StartServer` / `Connect` 로 역할을 잡은 뒤에 선다. `Disconnect` 는 그것을 되돌린다.
    // 역할에 따라 서버 쪽과 클라이언트 쪽 가운데 하나만 선다 - 한 프로세스가 둘 다일 수는 없다.
    //
    // 프레임 밖에서 `Update` 가 소켓을 돌리고, 복제 메시지는 안에서 처리하고, 게임 메시지는 모아 둔다. 고정 스텝 안에서는
    // `NetworkReceiveSystem`(가장 앞) 이 `ApplyClient` 를, `NetworkSendSystem`(가장 뒤) 이 `StepServer` 를 부른다.
    // 스크립트에는 `INetworkSystem` 으로만 보인다 - 컨텍스트 블록에 이 포인터가 실린다.
    class NetworkHost final : public System::INetworkSystem, public Network::IReplicationHost
    {
    public:
        // `provider` 는 소유하지 않는다. null 이면 소켓이 없는 플랫폼이다 - 모든 연결 시도가 거짓이고 서비스는 조용히 실패한다.
        NetworkHost(Network::ISocketProvider* provider, Network::IClock& clock, const Network::TransportConfig& transport = {},
            const Network::ReplicationConfig& replication = {});
        ~NetworkHost() override;

        NetworkHost(const NetworkHost&) = delete;
        NetworkHost& operator=(const NetworkHost&) = delete;

        // 오브젝트를 만들고 없애고 되찾는 자리. 캔버스를 기억만 하고 복제는 아직 세우지 않는다.
        void BindCanvas(Canvas* canvas);
        void UnbindCanvas();
        // 등록 순서가 곧 타입 번호다. 서버와 클라이언트가 같은 순서로 등록해야 하므로 호스트가 순서를 들고 있다가
        // 복제가 설 때 그대로 넘긴다. 프레임워크가 캔버스를 묶은 직후 부른다. 자리가 없으면 0xFF.
        std::uint8_t RegisterPool(Network::IReplicatedPool& pool);
        // 복제가 서 있는가. 역할이 없으면 거짓이다 - 테스트와 진단이 본다.
        bool IsReplicating() const;

        // 프레임 밖. 소켓을 돌리고 메시지를 가른다.
        void Update();
        // 고정 스텝 안. 각각 서버·클라이언트일 때만 일한다.
        void StepServer();
        // 보간 계수는 복제가 스냅숏 간격으로 스스로 정한다.
        void ApplyClient();

        Network::Transport& GetTransport();
        const NetworkSystemContext& GetSystemContext() const;
        const NetworkServiceContext& GetServiceContext() const;
        bool HasSockets() const;

        // ── INetworkSystem ──
        bool StartServer(std::uint16_t port) override;
        bool Connect(const char* host, std::uint16_t port) override;
        void Disconnect() override;
        Network::NetworkRole GetRole() const override;
        bool IsConnected() const override;
        std::uint32_t GetConnectionCount() const override;
        Network::ConnectionId GetConnectionAt(std::uint32_t index) const override;
        double GetRoundTripMilliseconds(Network::ConnectionId connection) const override;
        double GetUdpLossRate(Network::ConnectionId connection) const override;
        bool Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, std::uint32_t size,
            Network::NetChannel channel) override;
        bool Broadcast(Network::MessageId messageId, const void* data, std::uint32_t size, Network::NetChannel channel) override;
        std::uint32_t TakeEvents(Network::NetworkEvent* events, std::uint32_t capacity) override;
        std::uint32_t TakeMessages(Network::MessageView* messages, std::uint32_t capacity) override;
        Network::NetworkObjectId FindNetworkId(InstanceId object) const override;
        InstanceId FindLocalObject(Network::NetworkObjectId id) const override;
        bool HasAuthority(InstanceId object) const override;

        // ── IReplicationHost ──
        bool DescribeObject(InstanceId object, Network::SpawnDesc& outDesc) override;
        InstanceId SpawnObject(Network::NetworkObjectId id, const Network::SpawnDesc& desc) override;
        void DespawnObject(InstanceId object) override;

    private:
        // 소켓이 없는 플랫폼의 자리. 전부 null 을 돌려준다.
        class NullSocketProvider final : public Network::ISocketProvider
        {
        public:
            OwnerPtr<Network::IStreamSocket> CreateStreamSocket() override;
            OwnerPtr<Network::IDatagramSocket> CreateDatagramSocket() override;
            OwnerPtr<Network::IPeerConnection> CreatePeerConnection(const Network::PeerConnectionDesc& desc) override;
        };

        // 역할이 생기면 복제를 세우고 게임 메시지 자리를 잡는다. 역할이 없어지면 둘 다 돌려준다.
        void EnsureSession();
        void StopSession();

        NullSocketProvider m_nullProvider;
        Network::ISocketProvider* m_provider = nullptr;
        Network::Transport m_transport;
        Network::ReplicationConfig m_replicationConfig;
        Canvas* m_canvas = nullptr;
        // 등록된 순서 그대로다. 복제는 이 순서로 선다.
        Array<Network::IReplicatedPool*> m_pools;
        OwnerPtr<Network::ReplicationServer> m_server;
        OwnerPtr<Network::ReplicationClient> m_client;
        Network::ReplicationTick m_tick = 1;
        // 이번 프레임의 게임 메시지. 뷰는 트랜스포트의 수신 저장소를 가리키고 다음 `Update` 까지 산다.
        Array<Network::MessageView> m_gameMessages;
        std::uint32_t m_gameMessagesTaken = 0;
        // 이번 프레임에 자리가 없어 버린 게임 메시지가 있었다. 다음 `TakeEvents` 가 `Overflow` 로 알린다.
        bool m_overflowPending = false;
        NetworkSystemContext m_systemContext;
        NetworkServiceContext m_serviceContext;
    };
}
