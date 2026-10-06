#include <JBro/Package/PackageCook.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetSource.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/TextRendering/TextLibrary.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Package
{
    namespace
    {
        Entry EntryOf(const AssetRecord& record, BlobKind kind)
        {
            Entry entry;
            entry.id = record.id;
            entry.type = record.type;
            entry.kind = kind;
            entry.owner = record.owner;
            entry.path = record.relativePath;
            return entry;
        }

        void Fail(CookReport& report, const AssetRecord& record, const char* why)
        {
            String line(record.relativePath);
            line.append(": ");
            line.append(why);
            report.failures.Add(std::move(line));
        }

        Bool AddBlob(PackageWriter& writer, CookReport& report, const AssetRecord& record, BlobKind kind, const Array<std::byte>& bytes)
        {
            if (false == writer.Add(EntryOf(record, kind), ArrayView<const std::byte>(bytes.Data(), bytes.Size())))
            {
                Fail(report, record, "the asset appears twice in the package");
                return false;
            }
            report.blobBytes += bytes.Size();
            return true;
        }
    }

    Bool CookAssets(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot, ArrayView<const AssetId> ids,
        PackageWriter& writer, CookReport& report)
    {
        LooseAssetSource loose;
        loose.Bind(&platform, assetRoot);
        for (const AssetId& id : ids)
        {
            const AssetRecord* record = registry.Find(id);
            if (record == nullptr)
            {
                AssetRecord missing;
                char text[Uuid::TextCapacity] = {};
                id.ToText(text, sizeof(text));
                missing.relativePath = text;
                Fail(report, missing, "the asset is not in the registry");
                continue;
            }
            ++report.assets;
            // 이미지의 Sprite 는 주인의 메타와 픽셀을 쓴다. 레코드만 둔다.
            if (false == record->owner.IsNull())
            {
                if (false == writer.Add(EntryOf(*record, BlobKind::Record), {}))
                {
                    Fail(report, *record, "the asset appears twice in the package");
                }
                continue;
            }
            Array<std::byte> meta;
            if (false == loose.Read(*record, AssetBlob::Meta, meta))
            {
                Fail(report, *record, "the meta file could not be read");
                continue;
            }
            if (false == AddBlob(writer, report, *record, BlobKind::Meta, meta))
            {
                continue;
            }
            Array<std::byte> source;
            if (false == loose.Read(*record, AssetBlob::Source, source))
            {
                Fail(report, *record, "the source file could not be read");
                continue;
            }
            if (record->type == AssetType::Texture)
            {
                JArrayView<std::byte> view;
                view.data = source.Data();
                view.size = static_cast<JBro::UInt32>(source.Size());
                DecodedImage image;
                if (false == DecodeImage(view, image))
                {
                    Fail(report, *record, "the image could not be decoded");
                    continue;
                }
                Array<std::byte> cooked;
                WriteCookedTexture(image.width, image.height, image.pixels.Data(), cooked);
                if (AddBlob(writer, report, *record, BlobKind::CookedTexture, cooked))
                {
                    ++report.cookedTextures;
                }
                continue;
            }
            if (record->type == AssetType::Font)
            {
                AssetMetaFile parsed;
                AssetMetaError metaError;
                FontData font;
                if (ParseAssetMetaFile(reinterpret_cast<const char*>(meta.Data()), meta.Size(), parsed, metaError) && parsed.hasFontOptions)
                {
                    font.options = parsed.fontOptions;
                }
                // 샘플러는 아틀라스와 무관하다. 크기·퍼짐의 정리만 라이브러리와 같으면 된다.
                NormalizeFontOptions(font.options, TextureFilter::Nearest);
                font.bytes = std::move(source);
                Array<std::byte> atlas;
                if (TextLibrary::BakeFontAtlas(font, atlas) && AddBlob(writer, report, *record, BlobKind::FontAtlas, atlas))
                {
                    ++report.bakedAtlases;
                }
                AddBlob(writer, report, *record, BlobKind::Source, font.bytes);
                continue;
            }
            AddBlob(writer, report, *record, BlobKind::Source, source);
        }
        return report.failures.IsEmpty();
    }
}
