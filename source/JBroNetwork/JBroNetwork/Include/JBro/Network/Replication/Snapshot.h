#pragma once

#include <JBro/Network/Replication/ReplicationTypes.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro::Network
{
    // 스냅숏 항목 하나: 오브젝트 하나의 컴포넌트 타입 하나. 정렬 키는 (오브젝트, 타입)이다.
    struct SnapshotEntry
    {
        NetworkObjectId object = InvalidNetworkObjectId;
        std::uint8_t type = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    inline std::uint64_t SnapshotKey(NetworkObjectId object, std::uint8_t type)
    {
        return (static_cast<std::uint64_t>(object) << 8) | type;
    }

    inline std::uint64_t SnapshotKey(const SnapshotEntry& entry)
    {
        return SnapshotKey(entry.object, entry.type);
    }

    // 한 틱의 전체 상태. 항목은 키 오름차순으로 정렬돼 두 스냅숏의 차이를 두 포인터로 낸다. 예산은 `Reset` 이 한 번 잡는다.
    class Snapshot final
    {
    public:
        void Reset(std::uint32_t maxEntries, std::uint32_t byteCapacity);
        // 비우고 이 틱의 것으로 삼는다.
        void Begin(ReplicationTick tick);
        // 자리가 없으면 거짓이다.
        bool Add(NetworkObjectId object, std::uint8_t type, const std::uint8_t* bytes, std::uint32_t size);
        void Sort();
        // 다른 스냅숏의 내용을 그대로 든다. 용량은 같아야 한다.
        bool CopyFrom(const Snapshot& other);
        void Invalidate();

        bool IsValid() const;
        ReplicationTick Tick() const;
        std::uint32_t EntryCount() const;
        std::uint32_t ByteCount() const;
        const SnapshotEntry& EntryAt(std::uint32_t index) const;
        const std::uint8_t* Bytes(const SnapshotEntry& entry) const;
        // 정렬된 뒤에만 뜻이 있다.
        const SnapshotEntry* Find(NetworkObjectId object, std::uint8_t type) const;
        // 이 오브젝트의 항목이 하나라도 있는가.
        bool ContainsObject(NetworkObjectId object) const;

    private:
        ReplicationTick m_tick = 0;
        bool m_valid = false;
        Array<SnapshotEntry> m_entries;
        std::uint32_t m_count = 0;
        Array<std::uint8_t> m_bytes;
        std::uint32_t m_size = 0;
    };

    // 최근 틱들의 스냅숏 고리. 기준으로 되돌아볼 수 있는 범위다.
    class SnapshotHistory final
    {
    public:
        void Reset(std::uint32_t ticks, std::uint32_t maxEntries, std::uint32_t byteCapacity);
        // 이 틱의 슬롯을 비워 돌려준다. 그 자리에 있던 옛 스냅숏은 사라진다.
        Snapshot& Begin(ReplicationTick tick);
        const Snapshot* Find(ReplicationTick tick) const;
        Snapshot* Find(ReplicationTick tick);
        std::uint32_t Capacity() const;
        void Clear();

    private:
        Array<Snapshot> m_slots;
    };
}
