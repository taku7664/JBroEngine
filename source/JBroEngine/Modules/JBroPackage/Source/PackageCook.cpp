#include <JBro/Package/PackageCook.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetSource.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/Asset/ModelImport.h>

#include <cstring>
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

        // 굽는 동안 마지막으로 읽은 모델 하나를 든다. 하위 에셋은 모델 아래에 이어 오므로 모델마다 한 번 읽는다.
        struct ModelCache
        {
            AssetId id;
            Bool ok = false;
            ModelData model;
            Array<ModelSubAsset> listed;
        };

        const ModelData* DecodeOwner(IPlatform& platform, const LooseAssetSource& loose, const char* assetRoot, const AssetRecord& owner,
            ModelCache& cache, String& error)
        {
            if (cache.id != owner.id)
            {
                cache = ModelCache{};
                cache.id = owner.id;
                Array<std::byte> metaBytes;
                AssetMetaFile meta;
                AssetMetaError metaError;
                if (false == loose.Read(owner, AssetBlob::Meta, metaBytes)
                    || false == ParseAssetMetaFile(reinterpret_cast<const char*>(metaBytes.Data()), metaBytes.Size(), meta, metaError))
                {
                    error = "the model's meta file could not be read";
                    return nullptr;
                }
                cache.ok = DecodeModelFile(platform, assetRoot, owner.relativePath,
                    meta.hasModelOptions ? meta.modelOptions : ModelImportOptions{}, cache.model, error);
                if (cache.ok)
                {
                    ListModelSubAssets(cache.model, cache.listed);
                }
            }
            if (false == cache.ok && error.empty())
            {
                error = "the model could not be decoded";
            }
            return cache.ok ? &cache.model : nullptr;
        }

        // 모델의 하위 메시·내장 그림 하나를 굽는다. 실패는 보고에 적는다.
        void CookModelPart(IPlatform& platform, const LooseAssetSource& loose, const char* assetRoot, const AssetRegistry& registry,
            const AssetRecord& owner, const AssetRecord& record, ModelCache& cache, PackageWriter& writer, CookReport& report)
        {
            String error;
            const ModelData* model = DecodeOwner(platform, loose, assetRoot, owner, cache, error);
            if (model == nullptr)
            {
                Fail(report, record, error.c_str());
                return;
            }
            const ModelSubAsset* sub = FindModelSubAsset(cache.listed, record.type, record.name);
            if (sub == nullptr)
            {
                Fail(report, record, "the model no longer has this sub-asset - reopen the project in the editor to refresh its list");
                return;
            }
            Array<std::byte> cooked;
            if (record.type == AssetType::Material)
            {
                // 모델의 재질은 그림 칸의 아이디까지 풀어 `.jmat` 글자로 둔다 - 게임의 레코드에는 이름이 없어 스스로 풀 수 없다.
                UInt32 missing = 0;
                const MaterialDesc desc = MakeModelMaterialDesc(registry, owner, *model, cache.listed, sub->index.Get(), missing);
                String text;
                if (false == FormatMaterialFile(desc, text))
                {
                    Fail(report, record, "the material could not be written");
                    return;
                }
                cooked.Resize(text.size());
                std::memcpy(cooked.Data(), text.data(), text.size());
                if (AddBlob(writer, report, record, BlobKind::CookedMaterial, cooked))
                {
                    ++report.cookedMaterials;
                }
                return;
            }
            if (record.type == AssetType::Mesh)
            {
                const ModelMesh& mesh = model->meshes[sub->index.Get()];
                WriteCookedMesh(mesh.vertices, mesh.indices, mesh.hasUvs, mesh.hasTangents, FindModelMeshMaterialId(registry, owner, cache.listed, mesh), cooked);
                if (AddBlob(writer, report, record, BlobKind::CookedMesh, cooked))
                {
                    ++report.cookedMeshes;
                }
                return;
            }
            const ModelImage& image = model->images[sub->index.Get()];
            JArrayView<std::byte> view;
            view.data = image.embedded.Data();
            view.size = static_cast<JBro::UInt32>(image.embedded.Size());
            DecodedImage pixels;
            if (false == DecodeImage(view, pixels))
            {
                Fail(report, record, "the embedded image could not be decoded");
                return;
            }
            WriteCookedTexture(pixels.width, pixels.height, pixels.pixels.Data(), cooked);
            if (AddBlob(writer, report, record, BlobKind::CookedTexture, cooked))
            {
                ++report.cookedTextures;
            }
        }
    }

    Bool CookAssets(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot, ArrayView<const AssetId> ids,
        PackageWriter& writer, CookReport& report)
    {
        LooseAssetSource loose;
        loose.Bind(&platform, assetRoot);
        ModelCache models;
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
            // 모델의 하위 메시·내장 그림은 모델에서 꺼내 굽는다(6 단계). 재질은 아직 레코드만이다.
            const AssetRecord* owner = record->owner.IsNull() ? nullptr : registry.Find(record->owner);
            if (owner != nullptr && owner->type == AssetType::Model
                && (record->type == AssetType::Mesh || record->type == AssetType::Texture || record->type == AssetType::Material))
            {
                CookModelPart(platform, loose, assetRoot, registry, *owner, *record, models, writer, report);
                continue;
            }
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
            // 모델은 메타만이다 - 하위 에셋이 제 블롭을 갖고, 게임은 원본을 읽지 않는다.
            if (record->type == AssetType::Model)
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
