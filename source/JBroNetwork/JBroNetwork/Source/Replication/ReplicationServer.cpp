#include <JBro/Network/Replication/ReplicationServer.h>

#include <JBro/Network/Internal/UdpDatagram.h>

#include <cstring>

namespace JBro::Network
{
    namespace
    {
        constexpr std::uint32_t SpawnMessageBytes = 4 + sizeof(SpawnDesc);
        constexpr std::uint32_t DespawnMessageBytes = 4;
        constexpr std::uint32_t AckMessageBytes = 4;
    }

    ReplicationServer::PoolVisitor::PoolVisitor(ReplicationServer& server, std::uint8_t type, Snapshot& snapshot)
        : m_server(server)
        , m_type(type)
        , m_snapshot(snapshot)
    {
    }

    void ReplicationServer::PoolVisitor::Element(InstanceId object, const std::uint8_t* bytes)
    {
        ObjectRecord* record = m_server.FindRecord(object);
        if (nullptr == record)
        {
            SpawnDesc desc;
            if (false == m_server.m_host.DescribeObject(object, desc))
            {
                // 복제 대상이 아니다.
                return;
            }
            record = m_server.AddRecord(object, desc);
            if (nullptr == record)
            {
                return;
            }
        }
        record->seen = true;
        m_snapshot.Add(record->id, m_type, bytes, m_server.m_typeSizes[m_type]);
    }

    ReplicationServer::ReplicationServer(Transport& transport, IReplicationHost& host, const ReplicationConfig& config)
        : m_transport(transport)
        , m_host(host)
        , m_config(config)
    {
        m_pools.Reserve(m_config.maxTypes);
        m_typeSizes.Reserve(m_config.maxTypes);
        m_objects.Reserve(m_config.maxObjects);
        m_objectIndex.Reserve(m_config.maxObjects);
        m_history.Reset(m_config.historyTicks, m_config.maxEntriesPerSnapshot, m_config.snapshotBytes);
        m_clients.Reserve(m_transport.GetConfig().maxConnections);
        m_deltaBuffer.Resize(m_config.maxDeltaBytes);
        m_removalScratch.Resize(m_config.maxEntriesPerSnapshot);
    }

    std::uint8_t ReplicationServer::RegisterPool(IReplicatedPool& pool)
    {
        if (m_pools.Size() >= m_config.maxTypes || m_pools.Size() >= 0xFF)
        {
            return 0xFF;
        }
        m_pools.Add(&pool);
        m_typeSizes.Add(pool.ElementBytes());
        return static_cast<std::uint8_t>(m_pools.Size() - 1);
    }

    // ── 스텝 ────────────────────────────────────────────────────────────────────────────────────

    void ReplicationServer::Step(ReplicationTick tick)
    {
        // 1) 스냅숏. 새 오브젝트는 여기서 기록에 오른다.
        for (ObjectRecord& record : m_objects)
        {
            record.seen = false;
            record.isNew = false;
        }
        Snapshot& current = m_history.Begin(tick);
        for (std::size_t type = 0; type < m_pools.Size(); ++type)
        {
            PoolVisitor visitor(*this, static_cast<std::uint8_t>(type), current);
            m_pools[type]->Visit(visitor);
        }
        current.Sort();
        m_diagnostics.lastSnapshotEntries = current.EntryCount();
        m_diagnostics.lastSnapshotBytes = current.ByteCount();

        // 2) 사라진 오브젝트는 소멸이다. 새 오브젝트는 이미 있는 클라이언트에게 스폰이다.
        std::size_t index = 0;
        while (index < m_objects.Size())
        {
            ObjectRecord& record = m_objects[index];
            if (record.seen)
            {
                if (record.isNew)
                {
                    for (const ClientState& client : m_clients)
                    {
                        SendSpawn(client.connection, record);
                    }
                }
                ++index;
                continue;
            }
            for (const ClientState& client : m_clients)
            {
                SendDespawn(client.connection, record.id);
            }
            RemoveRecordAt(index);
        }

        // 3) 새로 온 클라이언트는 전부를 스폰으로 받는다. 떠난 클라이언트는 잊는다.
        SyncClients();

        // 4) 델타.
        SendDeltas(current);
        m_diagnostics.objects = static_cast<std::uint32_t>(m_objects.Size());
        m_diagnostics.clients = static_cast<std::uint32_t>(m_clients.Size());
    }

    void ReplicationServer::SyncClients()
    {
        // 떠난 연결.
        std::size_t index = 0;
        while (index < m_clients.Size())
        {
            if (m_transport.GetConnectionState(m_clients[index].connection) != ConnectionState::Connected)
            {
                m_clients.RemoveAt(index);
                continue;
            }
            ++index;
        }
        // 새 연결.
        const std::uint32_t connectionCount = m_transport.GetConnectionCount();
        for (std::uint32_t at = 0; at < connectionCount; ++at)
        {
            const ConnectionId id = m_transport.GetConnectionAt(at);
            if (m_transport.GetConnectionState(id) != ConnectionState::Connected)
            {
                continue;
            }
            bool known = false;
            for (const ClientState& client : m_clients)
            {
                if (client.connection == id)
                {
                    known = true;
                    break;
                }
            }
            if (known || m_clients.Size() >= m_transport.GetConfig().maxConnections)
            {
                continue;
            }
            ClientState& client = m_clients.Emplace();
            client.connection = id;
            client.ackedTick = NoBaselineTick;
            for (const ObjectRecord& record : m_objects)
            {
                SendSpawn(id, record);
            }
        }
    }

    void ReplicationServer::SendDeltas(const Snapshot& current)
    {
        m_diagnostics.lastDeltaBytes = 0;
        for (ClientState& client : m_clients)
        {
            const Snapshot* baseline = m_history.Find(client.ackedTick);
            if (nullptr == baseline)
            {
                ++m_diagnostics.fullSnapshotsSent;
            }
            const std::uint32_t written = DeltaCodec::Encode(baseline, current, m_deltaBuffer.Data(),
                static_cast<std::uint32_t>(m_deltaBuffer.Size()), m_removalScratch.Data(),
                static_cast<std::uint32_t>(m_removalScratch.Size()));
            if (0 == written)
            {
                ++m_diagnostics.oversizedTicks;
                continue;
            }
            if (m_transport.Send(client.connection, ReplicationDeltaMessage, m_deltaBuffer.Data(), written, NetChannel::UnreliableSequenced))
            {
                client.lastSentTick = current.Tick();
                client.hasSent = true;
                m_diagnostics.lastDeltaBytes += written;
            }
        }
    }

    bool ReplicationServer::HandleMessage(const MessageView& view)
    {
        if (false == IsReplicationMessage(view.messageId))
        {
            return false;
        }
        if (view.messageId == ReplicationAckMessage && view.size == AckMessageBytes)
        {
            const ReplicationTick acked = UdpProto::ReadU32(view.data);
            for (ClientState& client : m_clients)
            {
                if (client.connection != view.connection)
                {
                    continue;
                }
                // 뒤로 가는 ACK 는 뒤늦게 온 것이다. 보내지 않은 틱의 ACK 는 거짓말이다.
                const bool newer = client.ackedTick == NoBaselineTick || acked > client.ackedTick;
                if (newer && client.hasSent && acked <= client.lastSentTick)
                {
                    client.ackedTick = acked;
                }
                break;
            }
        }
        return true;
    }

    // ── 기록 ────────────────────────────────────────────────────────────────────────────────────

    ReplicationServer::ObjectRecord* ReplicationServer::FindRecord(InstanceId instance)
    {
        const std::uint32_t* index = m_objectIndex.Find(instance);
        if (nullptr == index)
        {
            return nullptr;
        }
        return &m_objects[*index];
    }

    ReplicationServer::ObjectRecord* ReplicationServer::FindRecordByNetworkId(NetworkObjectId id)
    {
        for (ObjectRecord& record : m_objects)
        {
            if (record.id == id)
            {
                return &record;
            }
        }
        return nullptr;
    }

    ReplicationServer::ObjectRecord* ReplicationServer::AddRecord(InstanceId instance, const SpawnDesc& desc)
    {
        if (m_objects.Size() >= m_config.maxObjects)
        {
            return nullptr;
        }
        ObjectRecord& record = m_objects.Emplace();
        record.id = m_nextObjectId++;
        if (InvalidNetworkObjectId == m_nextObjectId)
        {
            m_nextObjectId = 1;
        }
        record.instance = instance;
        record.desc = desc;
        record.isNew = true;
        m_objectIndex.InsertOrAssign(instance, static_cast<std::uint32_t>(m_objects.Size() - 1));
        return &record;
    }

    void ReplicationServer::RemoveRecordAt(std::size_t index)
    {
        m_objectIndex.Remove(m_objects[index].instance);
        const std::size_t last = m_objects.Size() - 1;
        if (index != last)
        {
            m_objects[index] = m_objects[last];
            m_objectIndex.InsertOrAssign(m_objects[index].instance, static_cast<std::uint32_t>(index));
        }
        m_objects.RemoveAt(last);
    }

    void ReplicationServer::SendSpawn(ConnectionId connection, const ObjectRecord& record)
    {
        std::uint8_t message[SpawnMessageBytes];
        UdpProto::WriteU32(message, record.id);
        std::memcpy(message + 4, &record.desc, sizeof(SpawnDesc));
        m_transport.Send(connection, ReplicationSpawnMessage, message, SpawnMessageBytes, NetChannel::ReliableOrdered);
    }

    void ReplicationServer::SendDespawn(ConnectionId connection, NetworkObjectId id)
    {
        std::uint8_t message[DespawnMessageBytes];
        UdpProto::WriteU32(message, id);
        m_transport.Send(connection, ReplicationDespawnMessage, message, DespawnMessageBytes, NetChannel::ReliableOrdered);
    }

    const ReplicationServerDiagnostics& ReplicationServer::GetDiagnostics() const
    {
        return m_diagnostics;
    }

    NetworkObjectId ReplicationServer::FindNetworkId(InstanceId object) const
    {
        const std::uint32_t* index = m_objectIndex.Find(object);
        if (nullptr == index)
        {
            return InvalidNetworkObjectId;
        }
        return m_objects[*index].id;
    }

    std::uint32_t ReplicationServer::GetObjectCount() const
    {
        return static_cast<std::uint32_t>(m_objects.Size());
    }
}
