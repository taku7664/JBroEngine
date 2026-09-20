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
    struct ReplicationServerDiagnostics
    {
        std::uint32_t objects = 0;
        std::uint32_t clients = 0;
        std::uint32_t lastSnapshotEntries = 0;
        std::uint32_t lastSnapshotBytes = 0;
        // 이번 스텝에 모든 클라이언트에게 보낸 델타 바이트 합.
        std::uint32_t lastDeltaBytes = 0;
        // 전체 스냅숏을 보낸 횟수(기준이 없거나 이력에서 밀렸다).
        std::uint32_t fullSnapshotsSent = 0;
        // 델타가 상한을 넘어 보내지 못한 틱 수.
        std::uint32_t oversizedTicks = 0;
        // 이번 스텝에 실제로 인코드한 횟수. 클라이언트 수가 아니라 **서로 다른 기준의 수**다.
        std::uint32_t lastDeltaEncodes = 0;
    };

    // 서버 쪽 복제(network-plan §2.6). 고정 스텝마다 등록된 풀을 스냅숏으로 찍고, 새 오브젝트는 스폰을, 사라진 오브젝트는 소멸을
    // 신뢰 채널로, 상태 차이는 클라이언트마다 그 클라이언트가 ACK 한 기준에 대한 델타로 `UnreliableSequenced` 로 보낸다.
    // 어떤 오브젝트가 복제 대상인지는 `IReplicationHost::DescribeObject` 가 정한다. 등록된 컴포넌트가 하나도 없는 오브젝트는
    // 와이어에 없고, 있던 것이 전부 떨어지면 소멸로 보인다.
    class ReplicationServer final
    {
    public:
        ReplicationServer(Transport& transport, IReplicationHost& host, const ReplicationConfig& config = {});

        // 양쪽이 같은 순서로 등록해야 한다. 돌려주는 타입 번호가 와이어에 실린다. 자리가 없으면 0xFF.
        std::uint8_t RegisterPool(IReplicatedPool& pool);

        // 한 고정 스텝. 시뮬레이션 뒤, 트랜스포트 `Update` 전에 부른다(송신 시스템 자리).
        void Step(ReplicationTick tick);
        // 트랜스포트에서 꺼낸 메시지 가운데 복제 것이면 처리하고 참을 돌려준다.
        bool HandleMessage(const MessageView& view);

        const ReplicationServerDiagnostics& GetDiagnostics() const;
        NetworkObjectId FindNetworkId(InstanceId object) const;
        std::uint32_t GetObjectCount() const;

    private:
        struct ObjectRecord
        {
            NetworkObjectId id = InvalidNetworkObjectId;
            InstanceId instance = InvalidInstanceId;
            SpawnDesc desc;
            bool seen = false;
            bool isNew = false;
        };

        struct ClientState
        {
            ConnectionId connection = InvalidConnectionId;
            ReplicationTick ackedTick = NoBaselineTick;
            ReplicationTick lastSentTick = 0;
            bool hasSent = false;
            // 이번 스텝에서 이미 보냈는가. 같은 기준을 ack 한 클라이언트끼리 인코드를 나눠 쓰기 위한 표시다.
            bool sentThisStep = false;
        };

        class PoolVisitor final : public IReplicatedPoolVisitor
        {
        public:
            PoolVisitor(ReplicationServer& server, std::uint8_t type, Snapshot& snapshot);
            void Element(InstanceId object, const std::uint8_t* bytes) override;

        private:
            ReplicationServer& m_server;
            std::uint8_t m_type;
            Snapshot& m_snapshot;
        };

        ObjectRecord* FindRecord(InstanceId instance);
        ObjectRecord* FindRecordByNetworkId(NetworkObjectId id);
        ObjectRecord* AddRecord(InstanceId instance, const SpawnDesc& desc);
        void RemoveRecordAt(std::size_t index);
        void SendSpawn(ConnectionId connection, const ObjectRecord& record);
        void SendDespawn(ConnectionId connection, NetworkObjectId id);
        void SyncClients();
        void SendDeltas(const Snapshot& current);

        Transport& m_transport;
        IReplicationHost& m_host;
        ReplicationConfig m_config;

        Array<IReplicatedPool*> m_pools;
        Array<std::uint32_t> m_typeSizes;

        Array<ObjectRecord> m_objects;
        Table<InstanceId, std::uint32_t> m_objectIndex;
        NetworkObjectId m_nextObjectId = 1;

        SnapshotHistory m_history;
        Array<ClientState> m_clients;
        Array<std::uint8_t> m_deltaBuffer;
        Array<DeltaCodec::Removal> m_removalScratch;
        ReplicationServerDiagnostics m_diagnostics;
    };
}
