#include <JBro/Network/Replication/Snapshot.h>

#include <algorithm>
#include <cstring>

namespace JBro::Network
{
    // ── Snapshot ────────────────────────────────────────────────────────────────────────────────

    void Snapshot::Reset(std::uint32_t maxEntries, std::uint32_t byteCapacity)
    {
        m_entries.Resize(maxEntries);
        m_bytes.Resize(byteCapacity);
        m_count = 0;
        m_size = 0;
        m_valid = false;
    }

    void Snapshot::Begin(ReplicationTick tick)
    {
        m_tick = tick;
        m_count = 0;
        m_size = 0;
        m_valid = true;
    }

    bool Snapshot::Add(NetworkObjectId object, std::uint8_t type, const std::uint8_t* bytes, std::uint32_t size)
    {
        if (m_count >= m_entries.Size() || m_bytes.Size() - m_size < size)
        {
            return false;
        }
        SnapshotEntry& entry = m_entries[m_count++];
        entry.object = object;
        entry.type = type;
        entry.offset = m_size;
        entry.size = size;
        if (size > 0)
        {
            std::memcpy(m_bytes.Data() + m_size, bytes, size);
        }
        m_size += size;
        return true;
    }

    void Snapshot::Sort()
    {
        std::sort(m_entries.Data(), m_entries.Data() + m_count, [](const SnapshotEntry& a, const SnapshotEntry& b)
        {
            return SnapshotKey(a) < SnapshotKey(b);
        });
    }

    bool Snapshot::CopyFrom(const Snapshot& other)
    {
        if (m_entries.Size() < other.m_count || m_bytes.Size() < other.m_size)
        {
            return false;
        }
        m_tick = other.m_tick;
        m_valid = other.m_valid;
        m_count = other.m_count;
        m_size = other.m_size;
        if (m_count > 0)
        {
            std::memcpy(m_entries.Data(), other.m_entries.Data(), m_count * sizeof(SnapshotEntry));
        }
        if (m_size > 0)
        {
            std::memcpy(m_bytes.Data(), other.m_bytes.Data(), m_size);
        }
        return true;
    }

    void Snapshot::Invalidate()
    {
        m_valid = false;
        m_count = 0;
        m_size = 0;
    }

    bool Snapshot::IsValid() const
    {
        return m_valid;
    }

    ReplicationTick Snapshot::Tick() const
    {
        return m_tick;
    }

    std::uint32_t Snapshot::EntryCount() const
    {
        return m_count;
    }

    std::uint32_t Snapshot::ByteCount() const
    {
        return m_size;
    }

    const SnapshotEntry& Snapshot::EntryAt(std::uint32_t index) const
    {
        return m_entries[index];
    }

    const std::uint8_t* Snapshot::Bytes(const SnapshotEntry& entry) const
    {
        return m_bytes.Data() + entry.offset;
    }

    const SnapshotEntry* Snapshot::Find(NetworkObjectId object, std::uint8_t type) const
    {
        const std::uint64_t key = SnapshotKey(object, type);
        std::uint32_t low = 0;
        std::uint32_t high = m_count;
        while (low < high)
        {
            const std::uint32_t middle = low + (high - low) / 2;
            const std::uint64_t middleKey = SnapshotKey(m_entries[middle]);
            if (middleKey == key)
            {
                return &m_entries[middle];
            }
            if (middleKey < key)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        return nullptr;
    }

    bool Snapshot::ContainsObject(NetworkObjectId object) const
    {
        // 타입 0 부터의 키 위치를 찾아 그 오브젝트의 첫 항목인지 본다.
        const std::uint64_t key = SnapshotKey(object, 0);
        std::uint32_t low = 0;
        std::uint32_t high = m_count;
        while (low < high)
        {
            const std::uint32_t middle = low + (high - low) / 2;
            if (SnapshotKey(m_entries[middle]) < key)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        return low < m_count && m_entries[low].object == object;
    }

    // ── SnapshotHistory ─────────────────────────────────────────────────────────────────────────

    void SnapshotHistory::Reset(std::uint32_t ticks, std::uint32_t maxEntries, std::uint32_t byteCapacity)
    {
        m_slots.Resize(ticks);
        for (Snapshot& slot : m_slots)
        {
            slot.Reset(maxEntries, byteCapacity);
        }
    }

    Snapshot& SnapshotHistory::Begin(ReplicationTick tick)
    {
        Snapshot& slot = m_slots[tick % m_slots.Size()];
        slot.Begin(tick);
        return slot;
    }

    const Snapshot* SnapshotHistory::Find(ReplicationTick tick) const
    {
        if (m_slots.IsEmpty() || tick == NoBaselineTick)
        {
            return nullptr;
        }
        const Snapshot& slot = m_slots[tick % m_slots.Size()];
        if (false == slot.IsValid() || slot.Tick() != tick)
        {
            return nullptr;
        }
        return &slot;
    }

    Snapshot* SnapshotHistory::Find(ReplicationTick tick)
    {
        if (m_slots.IsEmpty() || tick == NoBaselineTick)
        {
            return nullptr;
        }
        Snapshot& slot = m_slots[tick % m_slots.Size()];
        if (false == slot.IsValid() || slot.Tick() != tick)
        {
            return nullptr;
        }
        return &slot;
    }

    std::uint32_t SnapshotHistory::Capacity() const
    {
        return static_cast<std::uint32_t>(m_slots.Size());
    }

    void SnapshotHistory::Clear()
    {
        for (Snapshot& slot : m_slots)
        {
            slot.Invalidate();
        }
    }
}
