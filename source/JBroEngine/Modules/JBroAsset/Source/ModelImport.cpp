#include <JBro/Asset/ModelImport.h>

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Uuid.h>

#include <cstdint>

#include <string>

namespace JBro
{
    namespace
    {
        String JoinPath(const char* root, std::string_view relative)
        {
            String path(root != nullptr ? root : "");
            if (false == path.empty() && path.back() != '/' && path.back() != '\\')
            {
                path += "/";
            }
            path += String(relative);
            return path;
        }

        // 곁 파일은 모델 파일의 폴더 기준이다.
        struct SideReader
        {
            IPlatform* platform = nullptr;
            String folder;

            static Bool Read(void* user, const char* relativePath, Array<std::byte>& bytes)
            {
                const SideReader& self = *static_cast<const SideReader*>(user);
                return self.platform->ReadWholeFile(JoinPath(self.folder.c_str(), relativePath).c_str(), bytes);
            }
        };

        // 재질 이름은 파일 안에서 겹칠 수 있다(이름 없는 재질이 여럿이면 모두 `Material`). 키가 겹치지 않게 ` (2)` 를 붙인다.
        String UniqueKey(const Array<String>& taken, const String& wanted)
        {
            const auto used = [&](const String& key) {
                for (std::size_t index = 0; index < taken.Size(); ++index)
                {
                    if (taken[index] == key)
                    {
                        return true;
                    }
                }
                return false;
            };
            if (false == used(wanted))
            {
                return wanted;
            }
            for (UInt32 suffix = 2;; ++suffix)
            {
                String candidate = wanted;
                candidate += " (";
                candidate += String(std::to_string(suffix.Get()).c_str());
                candidate += ")";
                if (false == used(candidate))
                {
                    return candidate;
                }
            }
        }
    }

    void ListModelSubAssets(const ModelData& model, Array<ModelSubAsset>& result)
    {
        result.Clear();
        Array<String> taken;
        const auto add = [&](const char* kind, const String& name, AssetType type, std::size_t index) {
            ModelSubAsset sub;
            sub.key = UniqueKey(taken, String(kind) + name);
            sub.type = type;
            sub.index = static_cast<UInt32>(index);
            taken.Add(sub.key);
            result.Add(std::move(sub));
        };
        for (std::size_t index = 0; index < model.meshes.Size(); ++index)
        {
            add("Mesh/", model.meshes[index].name, AssetType::Mesh, index);
        }
        // `importMaterials` 가 꺼져 있으면 디코더가 재질·그림을 내지 않는다 - 여기서 다시 거르지 않는다(같은 조건을 두 곳에 두면 한쪽만 고쳐지는 날이 온다).
        for (std::size_t index = 0; index < model.materials.Size(); ++index)
        {
            add("Material/", model.materials[index].name, AssetType::Material, index);
        }
        for (std::size_t index = 0; index < model.images.Size(); ++index)
        {
            if (model.images[index].embedded.Size() > 0)
            {
                add("Texture/", model.images[index].name, AssetType::Texture, index);
            }
        }
    }

    const ModelSubAsset* FindModelSubAsset(const Array<ModelSubAsset>& listed, AssetType type, std::string_view name)
    {
        for (std::size_t index = 0; index < listed.Size(); ++index)
        {
            AssetType listedType = AssetType::Unknown;
            std::string_view listedName;
            if (listed[index].type == type && SplitSubAssetKey(listed[index].key, listedType, listedName) && listedName == name)
            {
                return &listed[index];
            }
        }
        return nullptr;
    }

    AssetId FindModelSubAssetId(const AssetRegistry& registry, const AssetRecord& model, const Array<ModelSubAsset>& listed,
        AssetType type, std::size_t index)
    {
        std::string_view name;
        for (std::size_t at = 0; at < listed.Size(); ++at)
        {
            AssetType listedType = AssetType::Unknown;
            if (listed[at].type == type && listed[at].index == index && SplitSubAssetKey(listed[at].key, listedType, name))
            {
                break;
            }
            name = {};
        }
        if (name.empty())
        {
            return {};
        }
        Array<AssetId> owned;
        registry.CollectOwned(model.id, owned);
        for (std::size_t at = 0; at < owned.Size(); ++at)
        {
            const AssetRecord* record = registry.Find(owned[at]);
            if (record != nullptr && record->type == type && record->name == name)
            {
                return record->id;
            }
        }
        return {};
    }

    AssetId FindModelMeshMaterialId(const AssetRegistry& registry, const AssetRecord& model, const Array<ModelSubAsset>& listed, const ModelMesh& mesh)
    {
        if (mesh.material < 0)
        {
            return {};
        }
        return FindModelSubAssetId(registry, model, listed, AssetType::Material, static_cast<std::size_t>(mesh.material.Get()));
    }

    namespace
    {
        // 모델 폴더 기준 경로를 에셋 폴더 기준으로 펴고 `.`·`..` 를 접는다. 폴더 밖으로 나가면 빈 글자다.
        String ResolveBesideModel(const String& modelPath, const String& relative)
        {
            Array<String> parts;
            const auto push = [&](std::string_view text) {
                std::size_t start = 0;
                while (start <= text.size())
                {
                    std::size_t end = text.find_first_of("/\\", start);
                    if (end == std::string_view::npos)
                    {
                        end = text.size();
                    }
                    const std::string_view part = text.substr(start, end - start);
                    if (part == "..")
                    {
                        if (parts.IsEmpty())
                        {
                            return false;
                        }
                        parts.RemoveAt(parts.Size() - 1);
                    }
                    else if (false == part.empty() && part != ".")
                    {
                        parts.Add(String(part));
                    }
                    start = end + 1;
                }
                return true;
            };
            const std::size_t slash = modelPath.find_last_of('/');
            if ((slash != String::npos && false == push(std::string_view(modelPath).substr(0, slash))) || false == push(relative))
            {
                return {};
            }
            String joined;
            for (std::size_t index = 0; index < parts.Size(); ++index)
            {
                if (index > 0)
                {
                    joined += "/";
                }
                joined += parts[index];
            }
            return joined;
        }
    }

    MaterialDesc MakeModelMaterialDesc(const AssetRegistry& registry, const AssetRecord& model, const ModelData& data,
        const Array<ModelSubAsset>& listed, std::size_t materialIndex, UInt32& missingTextures)
    {
        MaterialDesc desc;
        if (materialIndex >= data.materials.Size())
        {
            return desc;
        }
        const ModelMaterial& source = data.materials[materialIndex];
        desc.shader = MaterialShader::Standard;
        desc.baseColor = Color{source.baseColor[0], source.baseColor[1], source.baseColor[2], source.baseColor[3]};
        desc.metallic = source.metallic;
        desc.roughness = source.roughness;
        desc.emissive = Color{source.emissive[0], source.emissive[1], source.emissive[2], 1.0f};
        desc.alpha = source.alphaCutout ? MaterialAlpha::Cutout : MaterialAlpha::Opaque;
        desc.alphaCutoff = source.alphaCutoff;
        const auto textureOf = [&](ModelTextureSlot slot) -> AssetId {
            const Int32 image = source.images[static_cast<UInt32>(slot).Get()];
            if (image < 0 || static_cast<std::size_t>(image.Get()) >= data.images.Size())
            {
                return {};
            }
            const ModelImage& picture = data.images[static_cast<std::size_t>(image.Get())];
            if (picture.embedded.Size() > 0)
            {
                const AssetId id = FindModelSubAssetId(registry, model, listed, AssetType::Texture, static_cast<std::size_t>(image.Get()));
                missingTextures += id.IsNull() ? 1u : 0u;
                return id;
            }
            const String path = ResolveBesideModel(model.relativePath, picture.path);
            const AssetRecord* file = path.empty() ? nullptr : registry.FindByPath(path);
            if (file == nullptr || file->type != AssetType::Texture)
            {
                ++missingTextures;
                return {};
            }
            return file->id;
        };
        desc.baseColorTexture = textureOf(ModelTextureSlot::BaseColor);
        desc.metallicRoughnessTexture = textureOf(ModelTextureSlot::MetallicRoughness);
        desc.normalTexture = textureOf(ModelTextureSlot::Normal);
        desc.occlusionTexture = textureOf(ModelTextureSlot::Occlusion);
        desc.emissiveTexture = textureOf(ModelTextureSlot::Emissive);
        return desc;
    }

    TextureImportOptions ModelTextureDefaults()
    {
        TextureImportOptions options;
        options.mipmaps = true;
        return options;
    }

    UInt64 MakeSharedImageKey(UInt64 contentHash, const TextureImportOptions& options)
    {
        // 옵션 칸이 늘면 여기서 빌드가 멈춘다 - 새 칸을 열쇠에 섞을지 정하고 크기를 고친다.
        static_assert(sizeof(TextureImportOptions) == 3, "a new texture option must be mixed into the shared image key or left out on purpose");
        if (contentHash == 0)
        {
            return 0;
        }
        std::uint64_t key = 14695981039346656037ull;
        const auto mix = [&](std::uint64_t value, std::size_t bytes) {
            for (std::size_t at = 0; at < bytes; ++at)
            {
                key ^= (value >> (at * 8)) & 0xFF;
                key *= 1099511628211ull;
            }
        };
        mix(contentHash.Get(), 8);
        mix(static_cast<std::uint64_t>(options.filter), 1);
        mix(static_cast<std::uint64_t>(options.colorSpace), 1);
        mix(options.mipmaps ? 1u : 0u, 1);
        return key != 0 ? UInt64(key) : UInt64(1);
    }

    Bool DecodeModelFile(IPlatform& platform, const char* assetRoot, std::string_view relativePath, const ModelImportOptions& options,
        ModelData& model, String& error)
    {
        const String path = JoinPath(assetRoot, relativePath);
        Array<std::byte> bytes;
        if (false == platform.ReadWholeFile(path.c_str(), bytes))
        {
            error = "the model file could not be read";
            return false;
        }
        const std::size_t slash = relativePath.find_last_of("/\\");
        SideReader side;
        side.platform = &platform;
        side.folder = JoinPath(assetRoot, slash == std::string_view::npos ? std::string_view() : relativePath.substr(0, slash));
        ModelFileReader reader;
        reader.read = &SideReader::Read;
        reader.user = &side;
        return DecodeModel({bytes.Data(), static_cast<UInt32>(bytes.Size())}, ModelFormatFromPath(relativePath), options, reader, model, error);
    }

    Bool RefreshModelSubAssets(IPlatform& platform, const char* assetRoot, std::string_view relativePath,
        AssetMetaFile& meta, Bool& changed, String& error)
    {
        changed = false;
        ModelData model;
        if (false == DecodeModelFile(platform, assetRoot, relativePath, meta.hasModelOptions ? meta.modelOptions : ModelImportOptions{}, model, error))
        {
            return false;
        }

        Array<ModelSubAsset> listed;
        ListModelSubAssets(model, listed);
        Array<AssetSubEntry> merged;
        for (std::size_t index = 0; index < listed.Size(); ++index)
        {
            AssetSubEntry entry;
            entry.key = listed[index].key;
            const AssetSubEntry* previous = nullptr;
            for (std::size_t old = 0; old < meta.subAssets.Size(); ++old)
            {
                if (meta.subAssets[old].key == entry.key)
                {
                    previous = &meta.subAssets[old];
                    break;
                }
            }
            entry.id = previous != nullptr ? previous->id : Uuid::Generate();
            if (listed[index].type == AssetType::Texture)
            {
                // 해시는 파일에서 늘 새로 온다(그림이 바뀌면 묶음도 바뀐다). 옵션은 사용자가 고친 것이라 지킨다.
                entry.contentHash = model.images[listed[index].index.Get()].contentHash;
                entry.hasTextureOptions = true;
                entry.textureOptions = previous != nullptr && previous->hasTextureOptions ? previous->textureOptions : ModelTextureDefaults();
            }
            merged.Add(std::move(entry));
        }

        Bool same = meta.hasSubAssets && merged.Size() == meta.subAssets.Size();
        for (std::size_t index = 0; same && index < merged.Size(); ++index)
        {
            const AssetSubEntry& now = merged[index];
            const AssetSubEntry& old = meta.subAssets[index];
            same = now.key == old.key && now.id == old.id && now.contentHash == old.contentHash && now.hasTextureOptions == old.hasTextureOptions;
        }
        changed = false == same;
        meta.subAssets = std::move(merged);
        meta.hasSubAssets = true;
        return true;
    }
}
