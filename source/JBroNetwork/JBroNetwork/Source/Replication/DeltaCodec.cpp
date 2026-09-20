#include <JBro/Network/Replication/DeltaCodec.h>

#include <JBro/Network/Internal/UdpDatagram.h>

#include <cstring>

namespace JBro::Network::DeltaCodec
{
    namespace
    {
        constexpr std::uint64_t EndKey = ~static_cast<std::uint64_t>(0);

        void WriteHeader(std::uint8_t* out, const DeltaHeader& header)
        {
            UdpProto::WriteU32(out, header.tick);
            UdpProto::WriteU32(out + 4, header.baselineTick);
            UdpProto::WriteU16(out + 8, header.changedCount);
            UdpProto::WriteU16(out + 10, header.removedCount);
        }

        bool SameBytes(const Snapshot& a, const SnapshotEntry& ea, const Snapshot& b, const SnapshotEntry& eb)
        {
            if (ea.size != eb.size)
            {
                return false;
            }
            return 0 == std::memcmp(a.Bytes(ea), b.Bytes(eb), ea.size);
        }
    }

    bool ReadHeader(const std::uint8_t* data, std::uint32_t size, DeltaHeader& outHeader)
    {
        if (size < HeaderBytes)
        {
            return false;
        }
        outHeader.tick = UdpProto::ReadU32(data);
        outHeader.baselineTick = UdpProto::ReadU32(data + 4);
        outHeader.changedCount = UdpProto::ReadU16(data + 8);
        outHeader.removedCount = UdpProto::ReadU16(data + 10);
        return true;
    }

    std::uint32_t Encode(const Snapshot* baseline, const Snapshot& current, std::uint8_t* out, std::uint32_t capacity,
        Removal* removalScratch, std::uint32_t removalCapacity)
    {
        if (capacity < HeaderBytes)
        {
            return 0;
        }
        DeltaHeader header;
        header.tick = current.Tick();
        header.baselineTick = (nullptr != baseline && baseline->IsValid()) ? baseline->Tick() : NoBaselineTick;
        std::uint32_t written = HeaderBytes;
        std::uint32_t removed = 0;
        std::uint32_t changed = 0;

        const std::uint32_t baseCount = (nullptr != baseline && baseline->IsValid()) ? baseline->EntryCount() : 0;
        std::uint32_t b = 0;
        std::uint32_t c = 0;
        while (b < baseCount || c < current.EntryCount())
        {
            const std::uint64_t keyBase = b < baseCount ? SnapshotKey(baseline->EntryAt(b)) : EndKey;
            const std::uint64_t keyCurrent = c < current.EntryCount() ? SnapshotKey(current.EntryAt(c)) : EndKey;
            if (keyBase < keyCurrent)
            {
                // 기준에는 있고 지금은 없다 - 떨어진 컴포넌트다.
                if (removed >= removalCapacity)
                {
                    return 0;
                }
                removalScratch[removed].object = baseline->EntryAt(b).object;
                removalScratch[removed].type = baseline->EntryAt(b).type;
                ++removed;
                ++b;
                continue;
            }
            const SnapshotEntry& entry = current.EntryAt(c);
            bool write = true;
            if (keyBase == keyCurrent)
            {
                write = false == SameBytes(*baseline, baseline->EntryAt(b), current, entry);
                ++b;
            }
            ++c;
            if (false == write)
            {
                continue;
            }
            if (capacity - written < ChangedEntryHeaderBytes + entry.size || changed >= 0xFFFF)
            {
                return 0;
            }
            UdpProto::WriteU32(out + written, entry.object);
            out[written + 4] = entry.type;
            std::memcpy(out + written + ChangedEntryHeaderBytes, current.Bytes(entry), entry.size);
            written += ChangedEntryHeaderBytes + entry.size;
            ++changed;
        }
        if (capacity - written < removed * RemovedEntryBytes || removed >= 0xFFFF)
        {
            return 0;
        }
        for (std::uint32_t index = 0; index < removed; ++index)
        {
            UdpProto::WriteU32(out + written, removalScratch[index].object);
            out[written + 4] = removalScratch[index].type;
            written += RemovedEntryBytes;
        }
        header.changedCount = static_cast<std::uint16_t>(changed);
        header.removedCount = static_cast<std::uint16_t>(removed);
        WriteHeader(out, header);
        return written;
    }

    bool Decode(const std::uint8_t* data, std::uint32_t size, const std::uint32_t* typeSizes, std::uint8_t typeCount,
        const Snapshot* baseline, Snapshot& out, DeltaHeader& outHeader, Removal* removals, std::uint32_t removalCapacity,
        std::uint32_t& outRemovalCount, const std::uint8_t** changedScratch, std::uint32_t changedCapacity)
    {
        outRemovalCount = 0;
        if (false == ReadHeader(data, size, outHeader))
        {
            return false;
        }
        const bool full = outHeader.baselineTick == NoBaselineTick;
        if (false == full && (nullptr == baseline || false == baseline->IsValid() || baseline->Tick() != outHeader.baselineTick))
        {
            return false;
        }
        if (outHeader.changedCount > changedCapacity || outHeader.removedCount > removalCapacity)
        {
            return false;
        }
        // 1) changed 항목의 위치를 걷는다. 크기는 타입이 정한다.
        std::uint32_t offset = HeaderBytes;
        for (std::uint32_t index = 0; index < outHeader.changedCount; ++index)
        {
            if (size - offset < ChangedEntryHeaderBytes)
            {
                return false;
            }
            const std::uint8_t type = data[offset + 4];
            if (type >= typeCount)
            {
                return false;
            }
            const std::uint32_t entrySize = typeSizes[type];
            if (size - offset < ChangedEntryHeaderBytes + entrySize)
            {
                return false;
            }
            changedScratch[index] = data + offset;
            offset += ChangedEntryHeaderBytes + entrySize;
        }
        // 2) removed.
        if (size - offset < outHeader.removedCount * RemovedEntryBytes)
        {
            return false;
        }
        for (std::uint32_t index = 0; index < outHeader.removedCount; ++index)
        {
            removals[index].object = UdpProto::ReadU32(data + offset);
            removals[index].type = data[offset + 4];
            offset += RemovedEntryBytes;
        }
        outRemovalCount = outHeader.removedCount;

        // 3) 기준 ∪ changed − removed 를 키 오름차순으로 합친다. changed 가 기준을 덮는다.
        const std::uint32_t baseCount = full ? 0 : baseline->EntryCount();
        std::uint32_t b = 0;
        std::uint32_t c = 0;
        std::uint32_t r = 0;
        while (b < baseCount || c < outHeader.changedCount)
        {
            const std::uint64_t keyBase = b < baseCount ? SnapshotKey(baseline->EntryAt(b)) : EndKey;
            std::uint64_t keyChanged = EndKey;
            if (c < outHeader.changedCount)
            {
                keyChanged = SnapshotKey(UdpProto::ReadU32(changedScratch[c]), changedScratch[c][4]);
            }
            if (keyChanged <= keyBase)
            {
                const std::uint8_t type = changedScratch[c][4];
                if (false == out.Add(UdpProto::ReadU32(changedScratch[c]), type, changedScratch[c] + ChangedEntryHeaderBytes, typeSizes[type]))
                {
                    return false;
                }
                if (keyChanged == keyBase)
                {
                    ++b;
                }
                ++c;
                continue;
            }
            // 기준 항목. 지워졌는지 본다 - removed 도 오름차순이다.
            while (r < outRemovalCount && SnapshotKey(removals[r].object, removals[r].type) < keyBase)
            {
                ++r;
            }
            const bool removed = r < outRemovalCount && SnapshotKey(removals[r].object, removals[r].type) == keyBase;
            if (false == removed)
            {
                const SnapshotEntry& entry = baseline->EntryAt(b);
                if (false == out.Add(entry.object, entry.type, baseline->Bytes(entry), entry.size))
                {
                    return false;
                }
            }
            ++b;
        }
        return true;
    }
}
