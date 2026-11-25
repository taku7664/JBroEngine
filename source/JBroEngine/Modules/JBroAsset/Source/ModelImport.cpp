#include <JBro/Asset/ModelImport.h>

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Uuid.h>

#include <cmath>
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
        // 뼈대와 애니메이션(D-456). 키의 앞말은 타입 이름이다(`SplitSubAssetKey` 가 타입 표로 읽는다).
        for (std::size_t index = 0; index < model.skeletons.Size(); ++index)
        {
            add("Skeleton/", model.skeletons[index].name, AssetType::Skeleton, index);
        }
        for (std::size_t index = 0; index < model.animations.Size(); ++index)
        {
            add("AnimationClip/", model.animations[index].name, AssetType::AnimationClip, index);
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
        desc.alpha = source.alphaBlend ? MaterialAlpha::Blend : source.alphaCutout ? MaterialAlpha::Cutout : MaterialAlpha::Opaque;
        desc.alphaCutoff = source.alphaCutoff;
        desc.doubleSided = source.doubleSided;
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
        desc.baseColorTextureId = textureOf(ModelTextureSlot::BaseColor);
        desc.metallicRoughnessTextureId = textureOf(ModelTextureSlot::MetallicRoughness);
        desc.normalTextureId = textureOf(ModelTextureSlot::Normal);
        desc.occlusionTextureId = textureOf(ModelTextureSlot::Occlusion);
        desc.emissiveTextureId = textureOf(ModelTextureSlot::Emissive);
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

    Bool MakeModelSubRecord(const AssetMetaFile& meta, const AssetSubEntry& entry, std::string_view relativePath, AssetRecord& record)
    {
        AssetType type = AssetType::Unknown;
        std::string_view name;
        if (false == SplitSubAssetKey(entry.key, type, name))
        {
            return false;
        }
        record = AssetRecord{};
        record.id = entry.id;
        record.type = type;
        record.relativePath = String(relativePath);
        record.owner = meta.id;
        record.name = String(name);
        if (type == AssetType::Texture)
        {
            record.sharedImageKey = MakeSharedImageKey(entry.contentHash, entry.hasTextureOptions ? entry.textureOptions : ModelTextureDefaults());
        }
        return true;
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

    namespace
    {
        // 트랙의 키 하나가 든 값의 수다(회전은 쿼터니언 넷, 나머지는 셋).
        std::size_t TrackComponents(const ModelAnimationTrack& track)
        {
            return track.path == ModelTrackPath::Rotation ? 4 : 3;
        }

        // 트랙의 `time` 자리 값을 뽑는다. 키가 없으면 거짓이다.
        Bool SampleTrack(const ModelAnimationTrack& track, Float time, Float (&out)[4])
        {
            const std::size_t count = track.times.Size();
            const std::size_t components = TrackComponents(track);
            if (count == 0 || track.values.Size() < count * components)
            {
                return false;
            }
            const auto copyKey = [&](std::size_t key) {
                for (std::size_t c = 0; c < components; ++c)
                {
                    out[c] = track.values[key * components + c];
                }
            };
            if (time <= track.times[0])
            {
                copyKey(0);
                return true;
            }
            if (time >= track.times[count - 1])
            {
                copyKey(count - 1);
                return true;
            }
            std::size_t key = 0;
            while (key + 1 < count && track.times[key + 1] <= time)
            {
                ++key;
            }
            if (track.interpolation == ModelInterpolation::Step)
            {
                copyKey(key);
                return true;
            }
            const Float span = track.times[key + 1] - track.times[key];
            const Float t = span > 0.0f ? Float((time - track.times[key]) / span) : Float(0.0f);
            Float sign = 1.0f;
            if (components == 4)
            {
                // 짧은 쪽으로 돈다 - 두 쿼터니언이 반대쪽이면 뒤의 것을 뒤집는다.
                Float dot = 0.0f;
                for (std::size_t c = 0; c < 4; ++c)
                {
                    dot = dot + track.values[key * 4 + c] * track.values[(key + 1) * 4 + c];
                }
                sign = dot < 0.0f ? Float(-1.0f) : Float(1.0f);
            }
            Float length = 0.0f;
            for (std::size_t c = 0; c < components; ++c)
            {
                const Float from = track.values[key * components + c];
                const Float to = track.values[(key + 1) * components + c] * sign;
                out[c] = from + (to - from) * t;
                length = length + out[c] * out[c];
            }
            if (components == 4 && length > 0.0f)
            {
                const Float scale = 1.0f / std::sqrt(length.Get());
                for (std::size_t c = 0; c < 4; ++c)
                {
                    out[c] = out[c] * scale;
                }
            }
            return true;
        }
    }

    void CropModelAnimation(const ModelAnimation& source, Float start, Float end, ModelAnimation& result)
    {
        const Float duration = source.duration > 0.0f ? source.duration : Float(0.0f);
        Float from = start < 0.0f ? Float(0.0f) : (start > duration ? duration : start);
        Float to = end < 0.0f || end > duration ? duration : end;
        if (to < from)
        {
            to = from;
        }
        result.name = source.name;
        result.duration = to - from;
        result.tracks.Clear();
        for (const ModelAnimationTrack& track : source.tracks)
        {
            ModelAnimationTrack cropped;
            cropped.bone = track.bone;
            cropped.path = track.path;
            cropped.interpolation = track.interpolation;
            const std::size_t components = TrackComponents(track);
            Float value[4] = {};
            if (false == SampleTrack(track, from, value))
            {
                continue;
            }
            const auto addKey = [&](Float time, const Float (&values)[4]) {
                cropped.times.Add(time);
                for (std::size_t c = 0; c < components; ++c)
                {
                    cropped.values.Add(values[c]);
                }
            };
            addKey(0.0f, value);
            for (std::size_t key = 0; key < track.times.Size(); ++key)
            {
                if (track.times[key] > from && track.times[key] < to)
                {
                    Float inner[4] = {};
                    for (std::size_t c = 0; c < components; ++c)
                    {
                        inner[c] = track.values[key * components + c];
                    }
                    addKey(track.times[key] - from, inner);
                }
            }
            if (to > from && SampleTrack(track, to, value))
            {
                addKey(to - from, value);
            }
            result.tracks.Add(std::move(cropped));
        }
    }

    void ApplyModelClipSettings(const ModelAnimation& source, const AssetSubEntry* entry, ModelAnimation& clip, Bool& loop, Array<ModelClipEvent>& events)
    {
        events.Clear();
        if (entry == nullptr || false == entry->hasClip)
        {
            clip = source;
            loop = true;
            return;
        }
        CropModelAnimation(source, entry->clip.start, entry->clip.end, clip);
        loop = entry->clip.loop;
        // 클립 구간 밖의 이벤트는 불리지 않는다. 시간 차례로 둔다 - 런타임은 앞에서부터 훑는다.
        for (const ModelClipEvent& event : entry->clip.events)
        {
            if (event.time >= 0.0f && event.time <= clip.duration)
            {
                std::size_t at = events.Size();
                while (at > 0 && events[at - 1].time > event.time)
                {
                    --at;
                }
                events.Insert(at, event);
            }
        }
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
            // 소켓은 사용자가 적은 것이다(D-460). 같은 키의 뼈대·메시면 그대로 옮긴다 - 뼈 이름으로 적혀 있어 뼈 차례가 바뀌어도 산다.
            if (previous != nullptr && (listed[index].type == AssetType::Skeleton || listed[index].type == AssetType::Mesh))
            {
                entry.sockets = previous->sockets;
            }
            // 클립 설정도 사용자가 적은 것이다(D-462). 같은 키의 애니메이션이면 옮긴다.
            if (previous != nullptr && listed[index].type == AssetType::AnimationClip && previous->hasClip)
            {
                entry.hasClip = true;
                entry.clip = previous->clip;
            }
            merged.Add(std::move(entry));
        }
        // **나눈 클립은 파일에 없어도 남는다**(D-462). 사용자가 원본에서 떼어 만든 것이라 다시 가져와도 지운다면 일을 잃는다. 원본이 사라졌으면 읽을 때 경고한다.
        // 가져온 애니메이션과 키가 겹치면(원본에 같은 이름이 새로 생겼다) 나눈 쪽의 키에 번호를 붙인다 - 아이디는 그대로라 참조가 끊기지 않는다.
        for (std::size_t old = 0; old < meta.subAssets.Size(); ++old)
        {
            const AssetSubEntry& kept = meta.subAssets[old];
            AssetType keptType = AssetType::Unknown;
            std::string_view keptName;
            if (false == kept.hasClip || kept.clip.source.empty() || false == SplitSubAssetKey(kept.key, keptType, keptName)
                || keptType != AssetType::AnimationClip)
            {
                continue;
            }
            Array<String> taken;
            for (const AssetSubEntry& each : merged)
            {
                taken.Add(each.key);
            }
            AssetSubEntry entry = kept;
            entry.key = UniqueKey(taken, kept.key);
            merged.Add(std::move(entry));
        }

        Bool same = meta.hasSubAssets && merged.Size() == meta.subAssets.Size();
        for (std::size_t index = 0; same && index < merged.Size(); ++index)
        {
            const AssetSubEntry& now = merged[index];
            const AssetSubEntry& old = meta.subAssets[index];
            same = now.key == old.key && now.id == old.id && now.contentHash == old.contentHash && now.hasTextureOptions == old.hasTextureOptions
                && now.sockets.Size() == old.sockets.Size() && now.hasClip == old.hasClip;
        }
        changed = false == same;
        meta.subAssets = std::move(merged);
        meta.hasSubAssets = true;
        return true;
    }
}
