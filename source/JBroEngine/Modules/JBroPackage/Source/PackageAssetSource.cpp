#include <JBro/Package/PackageAssetSource.h>

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/ModelImport.h>
#include <JBro/Types/Table.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Package
{
    // 에셋 시스템의 블롭 번호를 패키지 블롭 종류로 그대로 바꿔 찾는다 - 둘이 어긋나면 엉뚱한 블롭을 읽으므로 빌드가 멈춘다.
    static_assert(static_cast<std::uint8_t>(AssetBlob::Meta) == static_cast<std::uint8_t>(BlobKind::Meta)
            && static_cast<std::uint8_t>(AssetBlob::Source) == static_cast<std::uint8_t>(BlobKind::Source)
            && static_cast<std::uint8_t>(AssetBlob::CookedTexture) == static_cast<std::uint8_t>(BlobKind::CookedTexture)
            && static_cast<std::uint8_t>(AssetBlob::FontAtlas) == static_cast<std::uint8_t>(BlobKind::FontAtlas)
            && static_cast<std::uint8_t>(AssetBlob::CompiledShader) == static_cast<std::uint8_t>(BlobKind::CompiledShader)
            && static_cast<std::uint8_t>(AssetBlob::CookedMesh) == static_cast<std::uint8_t>(BlobKind::CookedMesh)
            && static_cast<std::uint8_t>(AssetBlob::CookedMaterial) == static_cast<std::uint8_t>(BlobKind::CookedMaterial),
        "AssetBlob and BlobKind must keep the same numbers");

    namespace
    {
        constexpr char StreamPrefix[] = "jpak:";
        constexpr std::size_t StreamPrefixLength = sizeof(StreamPrefix) - 1;
    }

    PackageAssetSource::PackageAssetSource(const PackageReader& package) : m_package(package)
    {
    }

    Bool PackageAssetSource::Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const
    {
        out.Clear();
        const Entry* entry = m_package.Find(record.id, static_cast<BlobKind>(blob));
        return entry != nullptr && m_package.ReadBlob(*entry, out);
    }

    Bool PackageAssetSource::Has(const AssetRecord& record, AssetBlob blob) const
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

    UInt32 FillRegistry(const PackageReader& package, AssetRegistry& registry)
    {
        registry.Clear();
        // 모델의 하위 레코드는 색인에 이름과 같은 그림 열쇠가 없다 - 모델 메타(패키지에 원문으로 있다)의 목록에서 느슨한 스캔과 같은 길로 짓는다(material-plan 5 단계).
        // 읽히지 않는 메타면 하위 레코드는 색인의 것 그대로다(이름 없이도 쿡된 블롭은 아이디로 찾는다).
        Table<AssetId, AssetMetaFile> models;
        for (UInt32 row = 0; row < package.GetEntryCount(); ++row)
        {
            const Entry& entry = package.GetEntry(row);
            if (entry.type != AssetType::Model || entry.kind != BlobKind::Meta)
            {
                continue;
            }
            Array<std::byte> text;
            AssetMetaFile meta;
            AssetMetaError error;
            if (package.ReadBlob(entry, text) && ParseAssetMetaFile(reinterpret_cast<const char*>(text.Data()), text.Size(), meta, error))
            {
                models.TryAdd(entry.id, std::move(meta));
            }
        }
        UInt32 count = 0;
        for (UInt32 row = 0; row < package.GetEntryCount(); ++row)
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
            if (const AssetMetaFile* model = entry.owner.IsNull() ? nullptr : models.Find(entry.owner))
            {
                for (const AssetSubEntry& sub : model->subAssets)
                {
                    AssetRecord built;
                    if (sub.id == entry.id && MakeModelSubRecord(*model, sub, entry.path, built) && built.type == entry.type)
                    {
                        record = std::move(built);
                        break;
                    }
                }
            }
            if (registry.Register(record))
            {
                ++count;
            }
        }
        return count;
    }
}
