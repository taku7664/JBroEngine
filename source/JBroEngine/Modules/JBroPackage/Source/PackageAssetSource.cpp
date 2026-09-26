#include <JBro/Package/PackageAssetSource.h>

#include <cstring>

namespace JBro::Package
{
    namespace
    {
        constexpr char StreamPrefix[] = "jpak:";
        constexpr std::size_t StreamPrefixLength = sizeof(StreamPrefix) - 1;
    }

    PackageAssetSource::PackageAssetSource(const PackageReader& package) : m_package(package)
    {
    }

    bool PackageAssetSource::Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const
    {
        out.Clear();
        const Entry* entry = m_package.Find(record.id, static_cast<BlobKind>(blob));
        return entry != nullptr && m_package.ReadBlob(*entry, out);
    }

    bool PackageAssetSource::Has(const AssetRecord& record, AssetBlob blob) const
    {
        return m_package.Find(record.id, static_cast<BlobKind>(blob)) != nullptr;
    }

    String PackageAssetSource::MakeStreamPath(const AssetRecord& record) const
    {
        char id[Uuid::TextCapacity] = {};
        record.id.ToText(id, sizeof(id));
        String path(StreamPrefix);
        path.append(id);
        return path;
    }

    OwnerPtr<IFileStream> PackageAssetSource::OpenStream(const char* streamPath) const
    {
        if (streamPath == nullptr || std::strncmp(streamPath, StreamPrefix, StreamPrefixLength) != 0)
        {
            return {};
        }
        AssetId id;
        if (false == Uuid::Parse(streamPath + StreamPrefixLength, std::strlen(streamPath + StreamPrefixLength), id))
        {
            return {};
        }
        const Entry* entry = m_package.Find(id, BlobKind::Source);
        return entry != nullptr ? m_package.OpenBlobStream(*entry) : OwnerPtr<IFileStream>{};
    }

    std::uint32_t FillRegistry(const PackageReader& package, AssetRegistry& registry)
    {
        registry.Clear();
        std::uint32_t count = 0;
        for (std::uint32_t row = 0; row < package.GetEntryCount(); ++row)
        {
            const Entry& entry = package.GetEntry(row);
            // 한 에셋의 블롭은 이어져 있다. 첫 줄에서 한 번 등록한다.
            if (row > 0 && package.GetEntry(row - 1).id == entry.id)
            {
                continue;
            }
            AssetRecord record;
            record.id = entry.id;
            record.type = entry.type;
            record.relativePath = entry.path;
            record.owner = entry.owner;
            if (registry.Register(record))
            {
                ++count;
            }
        }
        return count;
    }
}
