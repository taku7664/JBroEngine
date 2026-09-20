#pragma once

#include <JBro/Network/Replication/DeltaCodec.h>
#include <JBro/Network/Replication/IReplicatedPool.h>
#include <JBro/Network/Replication/ReplicationTypes.h>
#include <JBro/Network/Replication/Snapshot.h>
#include <JBro/Network/Transport.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro::Network
{
    struct ReplicationClientDiagnostics
    {
        std::uint32_t objects = 0;
        ReplicationTick latestTick = 0;
        bool hasSnapshot = false;
        // 기준이 없거나 낡아서 버린 델타.
        std::uint32_t droppedDeltas = 0;
        std::uint32_t appliedDeltas = 0;
        std::uint32_t fullSnapshotsReceived = 0;
    };

    // 클라이언트 쪽 복제. 스폰·소멸을 받아 호스트로 오브젝트를 만들고 없애며, 델타를 기준에 얹어 스냅숏을 되살리고 ACK 한다.
    // 스냅숏이 진실이고 풀에 적용하는 것은 그 투영이다 - 스폰이 델타보다 늦게 와도 스폰 순간에 최신 스냅숏을 입힌다.
    // `Apply` 는 최근 두 스냅숏 사이를 `alpha` 로 보간할 기회를 풀에 준다. 매 프레임 렌더 시각에 맞춰 부른다.
    class ReplicationClient final
    {
    public:
        ReplicationClient(Transport& transport, IReplicationHost& host, const ReplicationConfig& config = {});

        // 서버와 같은 순서로 등록한다.
        std::uint8_t RegisterPool(IReplicatedPool& pool);

        bool HandleMessage(const MessageView& view);
        // 최신 스냅숏을 풀에 입힌다. 앞 스냅숏이 있으면 `from` 으로 함께 준다.
        void Apply(float alpha);

        const ReplicationClientDiagnostics& GetDiagnostics() const;
        InstanceId FindLocal(NetworkObjectId id) const;
        std::uint32_t GetObjectCount() const;

    private:
        void HandleSpawn(const MessageView& view);
        void HandleDespawn(const MessageView& view);
        void HandleDelta(const MessageView& view);
        void ApplyObjectFromLatest(NetworkObjectId id, InstanceId local);
        void SendAck(ReplicationTick tick);

        Transport& m_transport;
        IReplicationHost& m_host;
        ReplicationConfig m_config;

        Array<IReplicatedPool*> m_pools;
        Array<std::uint32_t> m_typeSizes;

        Table<NetworkObjectId, InstanceId> m_objects;
        SnapshotHistory m_history;
        ReplicationTick m_latestTick = 0;
        ReplicationTick m_previousTick = NoBaselineTick;
        bool m_hasLatest = false;
        bool m_dirty = false;

        Array<DeltaCodec::Removal> m_removals;
        std::uint32_t m_removalCount = 0;
        Array<const std::uint8_t*> m_changedScratch;
        ReplicationClientDiagnostics m_diagnostics;
    };
}
