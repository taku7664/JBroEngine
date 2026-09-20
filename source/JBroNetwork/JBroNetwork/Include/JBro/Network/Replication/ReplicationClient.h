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
    //
    // `Apply()` 는 최근 두 스냅숏 사이를 시간으로 보간해 풀에 입힌다. 스냅숏이 오는 간격을 스스로 재고(이동 평균),
    // 마지막 스냅숏 이후 흐른 시간을 그 간격으로 나눈 값이 `alpha` 다 - 한 스냅숏 뒤를 그리는 흔한 방식이다.
    // 간격을 아직 모르면(스냅숏 하나뿐이거나 시계가 멈춘 테스트) `alpha` 는 1 이고 최신 값을 그대로 쓴다.
    // 새 스냅숏도 없고 보간할 것도 없으면 아무것도 하지 않는다 - 같은 값을 매 프레임 다시 쓰지 않기 위해서다.
    class ReplicationClient final
    {
    public:
        ReplicationClient(Transport& transport, IReplicationHost& host, const ReplicationConfig& config = {});

        // 서버와 같은 순서로 등록한다.
        std::uint8_t RegisterPool(IReplicatedPool& pool);

        bool HandleMessage(const MessageView& view);
        // 매 프레임 부른다. 보간 계수는 스스로 정한다.
        void Apply();
        // `alpha` 를 직접 주는 자리. 테스트와, 시각을 스스로 아는 호출자를 위한 것이다.
        void Apply(float alpha);

        const ReplicationClientDiagnostics& GetDiagnostics() const;
        InstanceId FindLocal(NetworkObjectId id) const;
        std::uint32_t GetObjectCount() const;

    private:
        void HandleSpawn(const MessageView& view);
        void HandleDespawn(const MessageView& view);
        void HandleDelta(const MessageView& view);
        void ApplyObjectFromLatest(NetworkObjectId id, InstanceId local);
        void ApplySnapshots(float alpha);
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
        // 스냅숏이 온 시각과 그 간격의 이동 평균(ms). 둘 다 0 이면 아직 모른다.
        double m_lastSnapshotMilliseconds = 0.0;
        double m_snapshotIntervalMilliseconds = 0.0;

        Array<DeltaCodec::Removal> m_removals;
        std::uint32_t m_removalCount = 0;
        Array<const std::uint8_t*> m_changedScratch;
        ReplicationClientDiagnostics m_diagnostics;
    };
}
