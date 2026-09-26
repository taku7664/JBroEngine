#include <JBro/Package/PackageReader.h>

#include <cstring>

namespace JBro::Package
{
    namespace
    {
        template <typename T>
        T Take(const std::byte*& at)
        {
            T value;
            std::memcpy(&value, at, sizeof(value));
            at += sizeof(value);
            return value;
        }

        // 블롭 하나만 보이는 스트림이다. 제 파일 핸들을 들고, 읽은 바이트를 파일 자리로 푼다.
        class BlobStream final : public IFileStream
        {
        public:
            BlobStream(OwnerPtr<IFileStream> file, std::uint64_t begin, std::uint64_t size, std::uint64_t key)
                : m_file(std::move(file)), m_begin(begin), m_size(size), m_key(key)
            {
            }

            std::size_t Read(void* buffer, std::size_t bytes) override
            {
                const std::uint64_t left = m_size - m_position;
                const std::size_t wanted = static_cast<std::size_t>(bytes < left ? bytes : left);
                if (wanted == 0 || false == m_file->Seek(static_cast<std::int64_t>(m_begin + m_position), FileSeekOrigin::Begin))
                {
                    return 0;
                }
                const std::size_t read = m_file->Read(buffer, wanted);
                Obfuscate(m_key, m_begin + m_position, buffer, read);
                m_position += read;
                return read;
            }

            bool Seek(std::int64_t offset, FileSeekOrigin origin) override
            {
                std::int64_t base = 0;
                if (origin == FileSeekOrigin::Current)
                {
                    base = static_cast<std::int64_t>(m_position);
                }
                else if (origin == FileSeekOrigin::End)
                {
                    base = static_cast<std::int64_t>(m_size);
                }
                const std::int64_t target = base + offset;
                if (target < 0 || target > static_cast<std::int64_t>(m_size))
                {
                    return false;
                }
                m_position = static_cast<std::uint64_t>(target);
                return true;
            }

            std::int64_t Tell() const override
            {
                return static_cast<std::int64_t>(m_position);
            }

            std::int64_t GetSize() const override
            {
                return static_cast<std::int64_t>(m_size);
            }

        private:
            OwnerPtr<IFileStream> m_file;
            std::uint64_t m_begin = 0;
            std::uint64_t m_size = 0;
            std::uint64_t m_key = 0;
            std::uint64_t m_position = 0;
        };

        bool ReadAt(IFileStream& file, std::uint64_t offset, void* buffer, std::size_t size)
        {
            if (false == file.Seek(static_cast<std::int64_t>(offset), FileSeekOrigin::Begin))
            {
                return false;
            }
            return file.Read(buffer, size) == size;
        }
    }

    bool PackageReader::Open(IPlatform& platform, const char* utf8Path, String& error)
    {
        Close();
        OwnerPtr<IFileStream> file = platform.OpenFileStream(utf8Path);
        if (file.Get() == nullptr)
        {
            error = "the package file could not be opened";
            return false;
        }
        const std::int64_t fileSize = file->GetSize();
        std::byte header[HeaderSize] = {};
        if (fileSize < static_cast<std::int64_t>(HeaderSize) || false == ReadAt(*file, 0, header, HeaderSize))
        {
            error = "the file is too short to be a package";
            return false;
        }
        if (std::memcmp(header, Magic, sizeof(Magic)) != 0)
        {
            error = "the file is not a JBro package";
            return false;
        }
        const std::byte* at = header + sizeof(Magic);
        const auto version = Take<std::uint32_t>(at);
        const auto headerSize = Take<std::uint32_t>(at);
        const auto entryCount = Take<std::uint32_t>(at);
        (void)Take<std::uint32_t>(at);
        const auto indexOffset = Take<std::uint64_t>(at);
        const auto indexSize = Take<std::uint64_t>(at);
        const auto indexHash = Take<std::uint64_t>(at);
        const auto key = Take<std::uint64_t>(at);
        if (version != FormatVersion || headerSize != HeaderSize)
        {
            error = "the package was written by another format version";
            return false;
        }
        const auto total = static_cast<std::uint64_t>(fileSize);
        if (indexOffset < HeaderSize || indexOffset > total || indexSize > total - indexOffset
            || indexSize < static_cast<std::uint64_t>(entryCount) * RecordFixedSize)
        {
            error = "the package index lies outside the file";
            return false;
        }
        Array<std::byte> index;
        index.Resize(static_cast<std::size_t>(indexSize));
        if (false == ReadAt(*file, indexOffset, index.Data(), index.Size()))
        {
            error = "the package index could not be read";
            return false;
        }
        Obfuscate(key, indexOffset, index.Data(), index.Size());
        if (Hash(index.Data(), index.Size()) != indexHash)
        {
            error = "the package index is damaged";
            return false;
        }

        Array<Entry> entries;
        entries.Reserve(entryCount);
        const std::byte* cursor = index.Data();
        const std::byte* end = index.Data() + index.Size();
        for (std::uint32_t row = 0; row < entryCount; ++row)
        {
            if (static_cast<std::size_t>(end - cursor) < RecordFixedSize)
            {
                error = "the package index ends in the middle of a record";
                return false;
            }
            Entry entry;
            entry.id.high = Take<std::uint64_t>(cursor);
            entry.id.low = Take<std::uint64_t>(cursor);
            entry.type = static_cast<AssetType>(Take<std::uint16_t>(cursor));
            const auto kind = Take<std::uint8_t>(cursor);
            (void)Take<std::uint8_t>(cursor);
            const auto pathLength = Take<std::uint32_t>(cursor);
            entry.owner.high = Take<std::uint64_t>(cursor);
            entry.owner.low = Take<std::uint64_t>(cursor);
            entry.offset = Take<std::uint64_t>(cursor);
            entry.size = Take<std::uint64_t>(cursor);
            entry.hash = Take<std::uint64_t>(cursor);
            if (kind >= BlobKindCount || pathLength > MaxPathBytes || static_cast<std::size_t>(end - cursor) < pathLength)
            {
                error = "the package index holds a record this reader does not understand";
                return false;
            }
            entry.kind = static_cast<BlobKind>(kind);
            entry.path.assign(reinterpret_cast<const char*>(cursor), pathLength);
            cursor += pathLength;
            const bool record = entry.kind == BlobKind::Record;
            const bool placed = record
                ? entry.offset == 0 && entry.size == 0
                : entry.offset >= HeaderSize && entry.offset % BlobAlignment == 0 && entry.offset <= indexOffset
                    && entry.size <= indexOffset - entry.offset;
            if (entry.id.IsNull() || false == placed)
            {
                error = "a package record points outside the blob area";
                return false;
            }
            // 차례를 지켜야 한다 - 같은 (id, kind) 가 둘이거나 흩어져 있으면 찾기가 틀린다.
            if (false == entries.IsEmpty() && false == EntryLess(entries[entries.Size() - 1], entry))
            {
                error = "the package index is out of order or repeats a record";
                return false;
            }
            entries.Add(std::move(entry));
        }
        if (cursor != end)
        {
            error = "the package index has bytes after its last record";
            return false;
        }

        m_platform = &platform;
        m_path = utf8Path;
        m_file = std::move(file);
        m_key = key;
        m_entries = std::move(entries);
        for (std::uint32_t row = 0; row < m_entries.Size(); ++row)
        {
            if (row == 0 || m_entries[row - 1].id != m_entries[row].id)
            {
                m_firstById.TryAdd(m_entries[row].id, row);
            }
        }
        return true;
    }

    void PackageReader::Close()
    {
        m_file.Reset();
        m_platform = nullptr;
        m_path.clear();
        m_key = 0;
        m_entries.Clear();
        m_firstById.Clear();
    }

    bool PackageReader::IsOpen() const
    {
        return m_file.Get() != nullptr;
    }

    std::uint32_t PackageReader::GetEntryCount() const
    {
        return static_cast<std::uint32_t>(m_entries.Size());
    }

    const Entry& PackageReader::GetEntry(std::uint32_t index) const
    {
        return m_entries[index];
    }

    const Entry* PackageReader::Find(AssetId id, BlobKind kind) const
    {
        const std::uint32_t* first = m_firstById.Find(id);
        if (first == nullptr)
        {
            return nullptr;
        }
        for (std::uint32_t row = *first; row < m_entries.Size() && m_entries[row].id == id; ++row)
        {
            if (m_entries[row].kind == kind)
            {
                return &m_entries[row];
            }
        }
        return nullptr;
    }

    bool PackageReader::ReadBlob(const Entry& entry, Array<std::byte>& out) const
    {
        out.Clear();
        if (m_file.Get() == nullptr || entry.kind == BlobKind::Record)
        {
            return false;
        }
        out.Resize(static_cast<std::size_t>(entry.size));
        if (entry.size > 0 && false == ReadAt(*m_file, entry.offset, out.Data(), out.Size()))
        {
            out.Clear();
            return false;
        }
        Obfuscate(m_key, entry.offset, out.Data(), out.Size());
        if (Hash(out.Data(), out.Size()) != entry.hash)
        {
            out.Clear();
            return false;
        }
        return true;
    }

    OwnerPtr<IFileStream> PackageReader::OpenBlobStream(const Entry& entry) const
    {
        if (m_platform == nullptr || entry.kind == BlobKind::Record)
        {
            return {};
        }
        OwnerPtr<IFileStream> file = m_platform->OpenFileStream(m_path.c_str());
        if (file.Get() == nullptr)
        {
            return {};
        }
        return MakeOwnerPtr<BlobStream>(std::move(file), entry.offset, entry.size, m_key);
    }

    const String& PackageReader::GetPath() const
    {
        return m_path;
    }
}
