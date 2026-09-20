#include <JBro/NetworkSystem/NetworkHost.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/Ref.h>

namespace JBro
{
    namespace
    {
        GameObject* ResolveObject(InstanceId id)
        {
            if (InvalidInstanceId == id)
            {
                return nullptr;
            }
            const Internal::ResolvedInstance resolved = Internal::ResolveInstanceById(id, InvalidInstanceId, RefCategory::Object);
            return static_cast<GameObject*>(resolved.Pointer);
        }
    }

    OwnerPtr<Network::IStreamSocket> NetworkHost::NullSocketProvider::CreateStreamSocket()
    {
        return nullptr;
    }

    OwnerPtr<Network::IDatagramSocket> NetworkHost::NullSocketProvider::CreateDatagramSocket()
    {
        return nullptr;
    }

    OwnerPtr<Network::IPeerConnection> NetworkHost::NullSocketProvider::CreatePeerConnection(const Network::PeerConnectionDesc& desc)
    {
        (void)desc;
        return nullptr;
    }

    NetworkHost::NetworkHost(Network::ISocketProvider* provider, Network::IClock& clock, const Network::TransportConfig& transport,
        const Network::ReplicationConfig& replication)
        : m_provider(provider)
        , m_transport(nullptr != provider ? *provider : static_cast<Network::ISocketProvider&>(m_nullProvider), clock, transport)
        , m_replicationConfig(replication)
    {
        m_gameMessages.Reserve(m_transport.GetConfig().inboundMessages);
        m_systemContext.Network = this;
    }

    NetworkHost::~NetworkHost()
    {
        UnbindCanvas();
        m_transport.Close();
    }

    // ── 캔버스와 복제 ───────────────────────────────────────────────────────────────────────────

    void NetworkHost::BindCanvas(Canvas* canvas)
    {
        m_canvas = canvas;
        m_server = MakeOwnerPtr<Network::ReplicationServer>(m_transport, *this, m_replicationConfig);
        m_client = MakeOwnerPtr<Network::ReplicationClient>(m_transport, *this, m_replicationConfig);
        m_tick = 1;
    }

    void NetworkHost::UnbindCanvas()
    {
        m_server = nullptr;
        m_client = nullptr;
        m_canvas = nullptr;
    }

    std::uint8_t NetworkHost::RegisterPool(Network::IReplicatedPool& pool)
    {
        if (nullptr == m_server.Get() || nullptr == m_client.Get())
        {
            return 0xFF;
        }
        m_server->RegisterPool(pool);
        return m_client->RegisterPool(pool);
    }

    void NetworkHost::Update()
    {
        m_transport.Update();
        m_gameMessages.Clear();
        m_gameMessagesTaken = 0;
        Network::MessageView views[64];
        std::uint32_t got = 0;
        while ((got = m_transport.TakeMessages(views, 64)) > 0)
        {
            for (std::uint32_t index = 0; index < got; ++index)
            {
                const Network::MessageView& view = views[index];
                if (Network::IsReplicationMessage(view.messageId))
                {
                    if (m_transport.GetRole() == Network::NetworkRole::Server && nullptr != m_server.Get())
                    {
                        m_server->HandleMessage(view);
                    }
                    else if (m_transport.GetRole() == Network::NetworkRole::Client && nullptr != m_client.Get())
                    {
                        m_client->HandleMessage(view);
                    }
                    continue;
                }
                if (m_gameMessages.Size() < m_gameMessages.Capacity())
                {
                    m_gameMessages.Add(view);
                }
            }
        }
    }

    void NetworkHost::StepServer()
    {
        if (m_transport.GetRole() == Network::NetworkRole::Server && nullptr != m_server.Get())
        {
            m_server->Step(m_tick++);
        }
    }

    void NetworkHost::ApplyClient(float alpha)
    {
        if (m_transport.GetRole() == Network::NetworkRole::Client && nullptr != m_client.Get())
        {
            m_client->Apply(alpha);
        }
    }

    Network::Transport& NetworkHost::GetTransport()
    {
        return m_transport;
    }

    const NetworkSystemContext& NetworkHost::GetSystemContext() const
    {
        return m_systemContext;
    }

    const NetworkServiceContext& NetworkHost::GetServiceContext() const
    {
        return m_serviceContext;
    }

    bool NetworkHost::HasSockets() const
    {
        return nullptr != m_provider;
    }

    // ── INetworkSystem ──────────────────────────────────────────────────────────────────────────

    bool NetworkHost::StartServer(std::uint16_t port)
    {
        return m_transport.Listen(port);
    }

    bool NetworkHost::Connect(const char* host, std::uint16_t port)
    {
        return m_transport.Connect(host, port);
    }

    void NetworkHost::Disconnect()
    {
        m_transport.Close();
    }

    Network::NetworkRole NetworkHost::GetRole() const
    {
        return m_transport.GetRole();
    }

    bool NetworkHost::IsConnected() const
    {
        if (m_transport.GetRole() == Network::NetworkRole::Server)
        {
            return m_transport.IsListening();
        }
        return m_transport.GetConnectionState(Network::ServerConnectionId) == Network::ConnectionState::Connected;
    }

    std::uint32_t NetworkHost::GetConnectionCount() const
    {
        return m_transport.GetConnectionCount();
    }

    Network::ConnectionId NetworkHost::GetConnectionAt(std::uint32_t index) const
    {
        return m_transport.GetConnectionAt(index);
    }

    double NetworkHost::GetRoundTripMilliseconds(Network::ConnectionId connection) const
    {
        return m_transport.GetRoundTripMilliseconds(connection);
    }

    double NetworkHost::GetUdpLossRate(Network::ConnectionId connection) const
    {
        return m_transport.GetUdpLossRate(connection);
    }

    bool NetworkHost::Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, std::uint32_t size,
        Network::NetChannel channel)
    {
        if (Network::IsReplicationMessage(messageId))
        {
            return false;
        }
        return m_transport.Send(connection, messageId, data, size, channel);
    }

    bool NetworkHost::Broadcast(Network::MessageId messageId, const void* data, std::uint32_t size, Network::NetChannel channel)
    {
        if (Network::IsReplicationMessage(messageId))
        {
            return false;
        }
        return m_transport.Broadcast(messageId, data, size, channel);
    }

    std::uint32_t NetworkHost::TakeEvents(Network::NetworkEvent* events, std::uint32_t capacity)
    {
        return m_transport.TakeEvents(events, capacity);
    }

    std::uint32_t NetworkHost::TakeMessages(Network::MessageView* messages, std::uint32_t capacity)
    {
        std::uint32_t taken = 0;
        while (taken < capacity && m_gameMessagesTaken < m_gameMessages.Size())
        {
            messages[taken++] = m_gameMessages[m_gameMessagesTaken++];
        }
        return taken;
    }

    Network::NetworkObjectId NetworkHost::FindNetworkId(InstanceId object) const
    {
        if (nullptr == m_server.Get() || m_transport.GetRole() != Network::NetworkRole::Server)
        {
            return Network::InvalidNetworkObjectId;
        }
        return m_server->FindNetworkId(object);
    }

    InstanceId NetworkHost::FindLocalObject(Network::NetworkObjectId id) const
    {
        if (nullptr == m_client.Get() || m_transport.GetRole() != Network::NetworkRole::Client)
        {
            return InvalidInstanceId;
        }
        return m_client->FindLocal(id);
    }

    bool NetworkHost::HasAuthority(InstanceId object) const
    {
        (void)object;
        // 서버가 모든 권한을 갖는다. 소유 위임과 예측은 열어 둔 것이다(network-plan §3-7).
        return m_transport.GetRole() == Network::NetworkRole::Server;
    }

    // ── IReplicationHost ────────────────────────────────────────────────────────────────────────

    bool NetworkHost::DescribeObject(InstanceId object, Network::SpawnDesc& outDesc)
    {
        (void)object;
        // 프리팹으로 만드는 길은 열어 둔 것이다. 지금은 빈 오브젝트를 만들고 어댑터가 상태를 입히며 컴포넌트를 붙인다.
        outDesc = {};
        return true;
    }

    InstanceId NetworkHost::SpawnObject(Network::NetworkObjectId id, const Network::SpawnDesc& desc)
    {
        (void)id;
        (void)desc;
        if (nullptr == m_canvas)
        {
            return InvalidInstanceId;
        }
        GameObject* object = m_canvas->CreateObject(nullptr);
        if (nullptr == object)
        {
            return InvalidInstanceId;
        }
        return object->GetInstanceId();
    }

    void NetworkHost::DespawnObject(InstanceId object)
    {
        if (nullptr == m_canvas)
        {
            return;
        }
        GameObject* resolved = ResolveObject(object);
        if (nullptr != resolved)
        {
            m_canvas->DestroyObject(resolved);
        }
    }
}
