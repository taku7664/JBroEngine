#include <JBro/Package/PackageWriter.h>

#include <algorithm>
#include <cstring>

namespace JBro::Package
{
    namespace
    {
        void Put(Array<std::byte>& out, const void* data, std::size_t size)
        {
            const std::size_t at = out.Size();
            out.Resize(at + size);
            std::memcpy(out.Data() + at, data, size);
        }

        template <typename T>
        void PutValue(Array<std::byte>& out, T value)
        {
            Put(out, &value, sizeof(value));
        }

        std::uint64_t Align(std::uint64_t value)
        {
            return (value + BlobAlignment - 1) / BlobAlignment * BlobAlignment;
        }
    }

    PackageWriter::PackageWriter(std::uint64_t key) : m_key(key)
    {
    }

    bool PackageWriter::Add(const Entry& entry, ArrayView<const std::byte> blob)
    {
        if (entry.id.IsNull() || entry.path.size() > MaxPathBytes || static_cast<std::uint8_t>(entry.kind) >= BlobKindCount)
        {
            return false;
        }
        for (const Entry& existing : m_entries)
        {
            if (existing.id == entry.id && existing.kind == entry.kind)
            {
                return false;
            }
        }
        Entry added = entry;
        if (entry.kind == BlobKind::Record)
        {
            added.offset = 0;
            added.size = 0;
            added.hash = 0;
        }
        else
        {
            added.offset = m_blobs.Size();
            added.size = blob.Size();
            added.hash = Hash(blob.Data(), blob.Size());
            if (blob.Size() > 0)
            {
                Put(m_blobs, blob.Data(), blob.Size());
            }
        }
        m_entries.Add(std::move(added));
        return true;
    }

    std::uint32_t PackageWriter::GetEntryCount() const
    {
        return static_cast<std::uint32_t>(m_entries.Size());
    }

    void PackageWriter::Build(Array<std::byte>& file) const
    {
        file.Clear();
        Array<Entry> sorted = m_entries;
        std::sort(sorted.begin(), sorted.end(), EntryLess);

        // 머리 자리를 비워 두고 블롭을 16 바이트 자리에 놓는다.
        file.Resize(HeaderSize);
        std::memset(file.Data(), 0, HeaderSize);
        for (Entry& entry : sorted)
        {
            if (entry.kind == BlobKind::Record)
            {
                continue;
            }
            const std::uint64_t at = Align(file.Size());
            file.Resize(static_cast<std::size_t>(at));
            if (entry.size > 0)
            {
                Put(file, m_blobs.Data() + entry.offset, static_cast<std::size_t>(entry.size));
                Obfuscate(m_key, at, file.Data() + at, static_cast<std::size_t>(entry.size));
            }
            entry.offset = at;
        }
        // 빈 자리(정렬 틈)는 0 이다. 섞지 않는다 - 읽는 쪽이 보지 않는다.

        const std::uint64_t indexOffset = file.Size();
        Array<std::byte> index;
        for (const Entry& entry : sorted)
        {
            PutValue(index, entry.id.high);
            PutValue(index, entry.id.low);
            PutValue(index, static_cast<std::uint16_t>(entry.type));
            PutValue(index, static_cast<std::uint8_t>(entry.kind));
            PutValue(index, static_cast<std::uint8_t>(0));
            PutValue(index, static_cast<std::uint32_t>(entry.path.size()));
            PutValue(index, entry.owner.high);
            PutValue(index, entry.owner.low);
            PutValue(index, entry.offset);
            PutValue(index, entry.size);
            PutValue(index, entry.hash);
            Put(index, entry.path.data(), entry.path.size());
        }
        const std::uint64_t indexHash = Hash(index.Data(), index.Size());
        Obfuscate(m_key, indexOffset, index.Data(), index.Size());
        Put(file, index.Data(), index.Size());

        Array<std::byte> header;
        Put(header, Magic, sizeof(Magic));
        PutValue(header, FormatVersion);
        PutValue(header, HeaderSize);
        PutValue(header, static_cast<std::uint32_t>(sorted.Size()));
        PutValue(header, static_cast<std::uint32_t>(0));
        PutValue(header, indexOffset);
        PutValue(header, static_cast<std::uint64_t>(index.Size()));
        PutValue(header, indexHash);
        PutValue(header, m_key);
        PutValue(header, static_cast<std::uint64_t>(0));
        std::memcpy(file.Data(), header.Data(), HeaderSize);
    }

    bool PackageWriter::Save(IPlatform& platform, const char* utf8Path, String& error) const
    {
        Array<std::byte> file;
        Build(file);
        if (file.Size() > 0xFFFFFFFFull)
        {
            error = "the package is larger than 4 GB, which one write cannot hold";
            return false;
        }
        JArrayView<std::byte> view;
        view.data = file.Data();
        view.size = static_cast<std::uint32_t>(file.Size());
        if (false == platform.WriteWholeFile(utf8Path, view))
        {
            error = "the package file could not be written";
            return false;
        }
        return true;
    }
}
