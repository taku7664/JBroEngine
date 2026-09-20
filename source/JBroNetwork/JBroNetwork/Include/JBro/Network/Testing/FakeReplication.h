#pragma once

#include <JBro/Network/Replication/IReplicatedPool.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstdint>
#include <cstring>

namespace JBro::Network::Testing
{
    // 가짜 컴포넌트 풀. 오브젝트 식별자마다 고정 크기 바이트 블록 하나다. 부착 단계에서 `Canvas` 풀 어댑터가 이 자리에 선다.
    class FakeReplicatedPool final : public IReplicatedPool
    {
    public:
        static constexpr std::uint32_t MaxElementBytes = 64;

        struct Record
        {
            InstanceId object = InvalidInstanceId;
            std::uint8_t bytes[MaxElementBytes] = {};
            float lastAlpha = -1.0f;
            bool lastHadFrom = false;
            std::uint32_t applyCount = 0;
        };

        FakeReplicatedPool(std::uint32_t elementBytes, std::uint32_t capacity)
            : m_elementBytes(elementBytes)
        {
            m_records.Reserve(capacity);
            m_index.Reserve(capacity);
        }

        std::uint32_t ElementBytes() const override
        {
            return m_elementBytes;
        }

        void Visit(IReplicatedPoolVisitor& visitor) override
        {
            for (const Record& record : m_records)
            {
                visitor.Element(record.object, record.bytes);
            }
        }

        bool Apply(InstanceId object, const std::uint8_t* from, const std::uint8_t* to, float alpha) override
        {
            Record& record = GetOrCreate(object);
            std::memcpy(record.bytes, to, m_elementBytes);
            record.lastAlpha = alpha;
            record.lastHadFrom = nullptr != from;
            ++record.applyCount;
            return true;
        }

        void Detach(InstanceId object) override
        {
            Remove(object);
        }

        // ── 테스트가 서버 쪽 상태를 만들고 바꾸는 데 쓴다 ──
        Record& Set(InstanceId object, const void* bytes)
        {
            Record& record = GetOrCreate(object);
            std::memcpy(record.bytes, bytes, m_elementBytes);
            return record;
        }

        Record* Find(InstanceId object)
        {
            const std::uint32_t* index = m_index.Find(object);
            if (nullptr == index)
            {
                return nullptr;
            }
            return &m_records[*index];
        }

        void Remove(InstanceId object)
        {
            const std::uint32_t* found = m_index.Find(object);
            if (nullptr == found)
            {
                return;
            }
            const std::uint32_t index = *found;
            m_index.Remove(object);
            const std::size_t last = m_records.Size() - 1;
            if (index != last)
            {
                m_records[index] = m_records[last];
                m_index.InsertOrAssign(m_records[index].object, index);
            }
            m_records.RemoveAt(last);
        }

        std::uint32_t LiveCount() const
        {
            return static_cast<std::uint32_t>(m_records.Size());
        }

    private:
        Record& GetOrCreate(InstanceId object)
        {
            Record* existing = Find(object);
            if (nullptr != existing)
            {
                return *existing;
            }
            Record& record = m_records.Emplace();
            record.object = object;
            m_index.InsertOrAssign(object, static_cast<std::uint32_t>(m_records.Size() - 1));
            return record;
        }

        std::uint32_t m_elementBytes;
        Array<Record> m_records;
        Table<InstanceId, std::uint32_t> m_index;
    };

    // 가짜 호스트. 서버에서는 모든 오브젝트를 같은 프리팹으로 설명하고(제외 목록은 뺀다), 클라이언트에서는 지역 식별자를 발급한다.
    class FakeReplicationHost final : public IReplicationHost
    {
    public:
        struct Spawned
        {
            NetworkObjectId id = InvalidNetworkObjectId;
            InstanceId local = InvalidInstanceId;
            SpawnDesc desc;
        };

        explicit FakeReplicationHost(const Uuid& prefab)
            : m_prefab(prefab)
        {
        }

        void AttachPool(FakeReplicatedPool& pool)
        {
            m_pools.Add(&pool);
        }

        void ExcludeFromReplication(InstanceId object)
        {
            m_excluded.InsertOrAssign(object, true);
        }

        bool DescribeObject(InstanceId object, SpawnDesc& outDesc) override
        {
            if (nullptr != m_excluded.Find(object))
            {
                return false;
            }
            outDesc.prefab = m_prefab;
            outDesc.owner = InvalidConnectionId;
            outDesc.flags = static_cast<std::uint32_t>(object & 0xFFFFFFFFu);
            return true;
        }

        InstanceId SpawnObject(NetworkObjectId id, const SpawnDesc& desc) override
        {
            Spawned& spawned = m_spawned.Emplace();
            spawned.id = id;
            spawned.local = m_nextLocal++;
            spawned.desc = desc;
            return spawned.local;
        }

        void DespawnObject(InstanceId object) override
        {
            for (FakeReplicatedPool* pool : m_pools)
            {
                pool->Remove(object);
            }
            for (std::size_t index = 0; index < m_spawned.Size(); ++index)
            {
                if (m_spawned[index].local == object)
                {
                    m_spawned.RemoveAt(index);
                    ++m_despawnCount;
                    return;
                }
            }
        }

        const Spawned* FindByNetworkId(NetworkObjectId id) const
        {
            for (const Spawned& spawned : m_spawned)
            {
                if (spawned.id == id)
                {
                    return &spawned;
                }
            }
            return nullptr;
        }

        std::uint32_t SpawnedCount() const
        {
            return static_cast<std::uint32_t>(m_spawned.Size());
        }

        std::uint32_t DespawnCount() const
        {
            return m_despawnCount;
        }

    private:
        Uuid m_prefab;
        Array<FakeReplicatedPool*> m_pools;
        Table<InstanceId, bool> m_excluded;
        Array<Spawned> m_spawned;
        InstanceId m_nextLocal = 1000;
        std::uint32_t m_despawnCount = 0;
    };
}
