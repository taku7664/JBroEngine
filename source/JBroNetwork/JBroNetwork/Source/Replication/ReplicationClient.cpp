#include <JBro/Network/Replication/ReplicationClient.h>

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

    ReplicationClient::ReplicationClient(Transport& transport, IReplicationHost& host, const ReplicationConfig& config)
        : m_transport(transport)
        , m_host(host)
        , m_config(config)
    {
        m_pools.Reserve(m_config.maxTypes);
        m_typeSizes.Reserve(m_config.maxTypes);
        m_objects.Reserve(m_config.maxObjects);
        m_history.Reset(m_config.historyTicks, m_config.maxEntriesPerSnapshot, m_config.snapshotBytes);
        m_removals.Resize(m_config.maxEntriesPerSnapshot);
        m_changedScratch.Resize(m_config.maxEntriesPerSnapshot);
    }

    std::uint8_t ReplicationClient::RegisterPool(IReplicatedPool& pool)
    {
        if (m_pools.Size() >= m_config.maxTypes || m_pools.Size() >= 0xFF)
        {
            return 0xFF;
        }
        m_pools.Add(&pool);
        m_typeSizes.Add(pool.ElementBytes());
        return static_cast<std::uint8_t>(m_pools.Size() - 1);
    }

    bool ReplicationClient::HandleMessage(const MessageView& view)
    {
        if (false == IsReplicationMessage(view.messageId))
        {
            return false;
        }
        switch (view.messageId)
        {
        case ReplicationSpawnMessage:
        {
            HandleSpawn(view);
            return true;
        }
        case ReplicationDespawnMessage:
        {
            HandleDespawn(view);
            return true;
        }
        case ReplicationDeltaMessage:
        {
            HandleDelta(view);
            return true;
        }
        default:
        {
            return true;
        }
        }
    }

    void ReplicationClient::HandleSpawn(const MessageView& view)
    {
        if (view.size != SpawnMessageBytes)
        {
            return;
        }
        const NetworkObjectId id = UdpProto::ReadU32(view.data);
        SpawnDesc desc;
        std::memcpy(&desc, view.data + 4, sizeof(SpawnDesc));
        if (nullptr != m_objects.Find(id))
        {
            return;
        }
        const InstanceId local = m_host.SpawnObject(id, desc);
        if (InvalidInstanceId == local)
        {
            return;
        }
        m_objects.InsertOrAssign(id, local);
        // 델타가 먼저 와 있었을 수 있다. 최신 스냅숏의 것을 바로 입힌다.
        ApplyObjectFromLatest(id, local);
        m_diagnostics.objects = static_cast<std::uint32_t>(m_objects.Size());
    }

    void ReplicationClient::HandleDespawn(const MessageView& view)
    {
        if (view.size != DespawnMessageBytes)
        {
            return;
        }
        const NetworkObjectId id = UdpProto::ReadU32(view.data);
        InstanceId* local = m_objects.Find(id);
        if (nullptr == local)
        {
            return;
        }
        m_host.DespawnObject(*local);
        m_objects.Remove(id);
        m_diagnostics.objects = static_cast<std::uint32_t>(m_objects.Size());
    }

    void ReplicationClient::HandleDelta(const MessageView& view)
    {
        DeltaCodec::DeltaHeader header;
        if (false == DeltaCodec::ReadHeader(view.data, view.size, header))
        {
            return;
        }
        // 낡은 틱은 버린다. Sequenced 채널이 대부분 걸러 주지만 WS 폴백 경로도 있다.
        if (m_hasLatest && header.tick <= m_latestTick)
        {
            ++m_diagnostics.droppedDeltas;
            return;
        }
        const Snapshot* baseline = nullptr;
        if (header.baselineTick != NoBaselineTick)
        {
            baseline = m_history.Find(header.baselineTick);
            // 기준이 없거나, 이 틱이 기준과 같은 슬롯을 쓴다면(이력 길이만큼 벌어짐) 기준을 덮어쓰게 된다. 버린다 - 서버가 전체를 다시 보낸다.
            if (nullptr == baseline || (header.tick % m_history.Capacity()) == (header.baselineTick % m_history.Capacity()))
            {
                ++m_diagnostics.droppedDeltas;
                return;
            }
        }
        else
        {
            ++m_diagnostics.fullSnapshotsReceived;
        }
        Snapshot& out = m_history.Begin(header.tick);
        if (false == DeltaCodec::Decode(view.data, view.size, m_typeSizes.Data(), static_cast<std::uint8_t>(m_typeSizes.Size()),
                baseline, out, header, m_removals.Data(), static_cast<std::uint32_t>(m_removals.Size()), m_removalCount,
                m_changedScratch.Data(), static_cast<std::uint32_t>(m_changedScratch.Size())))
        {
            out.Invalidate();
            ++m_diagnostics.droppedDeltas;
            return;
        }
        m_previousTick = m_hasLatest ? m_latestTick : NoBaselineTick;
        m_latestTick = header.tick;
        m_hasLatest = true;
        m_dirty = true;
        ++m_diagnostics.appliedDeltas;
        m_diagnostics.latestTick = header.tick;
        m_diagnostics.hasSnapshot = true;
        // 지운 항목은 지금 풀에서 떼어 낸다. 스냅숏에서는 이미 없다.
        for (std::uint32_t index = 0; index < m_removalCount; ++index)
        {
            const DeltaCodec::Removal& removal = m_removals[index];
            const InstanceId* local = m_objects.Find(removal.object);
            if (nullptr != local && removal.type < m_pools.Size())
            {
                m_pools[removal.type]->Detach(*local);
            }
        }
        m_removalCount = 0;
        SendAck(header.tick);
    }

    void ReplicationClient::SendAck(ReplicationTick tick)
    {
        std::uint8_t message[AckMessageBytes];
        UdpProto::WriteU32(message, tick);
        m_transport.Send(ServerConnectionId, ReplicationAckMessage, message, AckMessageBytes, NetChannel::UnreliableSequenced);
    }

    void ReplicationClient::Apply(float alpha)
    {
        if (false == m_hasLatest)
        {
            return;
        }
        const Snapshot* latest = m_history.Find(m_latestTick);
        if (nullptr == latest)
        {
            return;
        }
        const Snapshot* previous = m_history.Find(m_previousTick);
        for (std::uint32_t index = 0; index < latest->EntryCount(); ++index)
        {
            const SnapshotEntry& entry = latest->EntryAt(index);
            const InstanceId* local = m_objects.Find(entry.object);
            if (nullptr == local || entry.type >= m_pools.Size())
            {
                continue;
            }
            const std::uint8_t* from = nullptr;
            if (nullptr != previous)
            {
                const SnapshotEntry* before = previous->Find(entry.object, entry.type);
                if (nullptr != before)
                {
                    from = previous->Bytes(*before);
                }
            }
            m_pools[entry.type]->Apply(*local, from, latest->Bytes(entry), alpha);
        }
        m_dirty = false;
    }

    void ReplicationClient::ApplyObjectFromLatest(NetworkObjectId id, InstanceId local)
    {
        if (false == m_hasLatest)
        {
            return;
        }
        const Snapshot* latest = m_history.Find(m_latestTick);
        if (nullptr == latest)
        {
            return;
        }
        for (std::size_t type = 0; type < m_pools.Size(); ++type)
        {
            const SnapshotEntry* entry = latest->Find(id, static_cast<std::uint8_t>(type));
            if (nullptr != entry)
            {
                m_pools[type]->Apply(local, nullptr, latest->Bytes(*entry), 1.0f);
            }
        }
    }

    const ReplicationClientDiagnostics& ReplicationClient::GetDiagnostics() const
    {
        return m_diagnostics;
    }

    InstanceId ReplicationClient::FindLocal(NetworkObjectId id) const
    {
        const InstanceId* local = m_objects.Find(id);
        if (nullptr == local)
        {
            return InvalidInstanceId;
        }
        return *local;
    }

    std::uint32_t ReplicationClient::GetObjectCount() const
    {
        return static_cast<std::uint32_t>(m_objects.Size());
    }
}
