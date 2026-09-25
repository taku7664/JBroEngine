#include <JBro/Core/Log.h>
#include <JBro/Asset/Asset.h>

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Asset/AudioDecoder.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/Asset/SpriteFrames.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Types/NameTable.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace JBro
{
    namespace
    {
        constexpr AssetHandle MakeHandle(AssetType type, std::uint32_t slotIndex, std::uint32_t generation) noexcept
        {
            AssetHandle handle;
            handle.index = (static_cast<std::uint32_t>(type) << 28) | slotIndex;
            handle.generation = generation;
            return handle;
        }

        bool EndsWith(std::string_view text, std::string_view suffix) noexcept
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    }

    bool AssetSystem::Initialize(const JMemoryContext&)
    {
        return true;
    }

    void AssetSystem::Shutdown()
    {
        Unbind();
    }

    void AssetSystem::Bind(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot)
    {
        Unbind();
        m_platform = &platform;
        m_registry = &registry;
        m_assetRoot = assetRoot != nullptr ? assetRoot : "";
    }

    void AssetSystem::Unbind()
    {
        // 믹서가 빌려 쓰는 오디오 자료를 풀기 전에 알린다.
        for (std::uint32_t index = 0; index < m_audio.slots.Size(); ++index)
        {
            NotifyAudioRelease(index);
        }
        m_audio = {};
        m_textures = {};
        m_sprites = {};
        m_loaded.Clear();
        m_metaCache.Clear();
        m_platform = nullptr;
        m_registry = nullptr;
        m_assetRoot.clear();
    }

    bool AssetSystem::IsBound() const
    {
        return m_platform != nullptr && m_registry != nullptr;
    }

    AssetType AssetSystem::GetHandleType(AssetHandle handle)
    {
        if (handle.generation == 0)
        {
            return AssetType::Unknown;
        }
        return static_cast<AssetType>(handle.index >> TypeShift);
    }

    template <typename TData>
    AssetSystem::Slot<TData>* AssetSystem::FindSlot(Pool<TData>& pool, AssetHandle handle, AssetType type)
    {
        if (handle.generation == 0 || GetHandleType(handle) != type)
        {
            return nullptr;
        }
        const std::uint32_t slotIndex = handle.index & SlotMask;
        if (slotIndex >= pool.slots.Size())
        {
            return nullptr;
        }
        Slot<TData>& slot = pool.slots[slotIndex];
        return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
    }

    template <typename TData>
    const AssetSystem::Slot<TData>* AssetSystem::FindSlot(const Pool<TData>& pool, AssetHandle handle, AssetType type) const
    {
        return const_cast<AssetSystem*>(this)->FindSlot(const_cast<Pool<TData>&>(pool), handle, type);
    }

    template <typename TData>
    AssetHandle AssetSystem::Occupy(Pool<TData>& pool, AssetType type, AssetId id, TData&& data)
    {
        std::uint32_t slotIndex = 0;
        if (false == pool.freeList.IsEmpty())
        {
            slotIndex = pool.freeList.Pop();
        }
        else
        {
            if (pool.slots.Size() >= SlotMask)
            {
                return {};
            }
            slotIndex = static_cast<std::uint32_t>(pool.slots.Size());
            pool.slots.Emplace();
        }
        Slot<TData>& slot = pool.slots[slotIndex];
        slot.data = std::move(data);
        slot.id = id;
        slot.referenceCount = 1;
        slot.occupied = true;
        return MakeHandle(type, slotIndex, slot.generation);
    }

    template <typename TData>
    void AssetSystem::Vacate(Pool<TData>& pool, std::uint32_t slotIndex)
    {
        Slot<TData>& slot = pool.slots[slotIndex];
        m_loaded.Remove(slot.id);
        // 내려간 것의 메타는 잊는다. 다음 로드는 디스크를 본다 - 손으로 고친 `.jmeta` 는 감시가 무시하므로 여기가 그 길이다.
        ForgetMeta(slot.id);
        slot.data = TData{};
        slot.id = {};
        slot.referenceCount = 0;
        slot.occupied = false;
        // 세대를 올려 옛 핸들을 무효로 만든다. 0 은 빈 핸들의 표지라 건너뛴다.
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        pool.freeList.Add(slotIndex);
    }

    String AssetSystem::SourcePathOf(const AssetRecord& record) const
    {
        String path = m_assetRoot;
        if (false == path.empty() && path.back() != '/' && path.back() != '\\')
        {
            path.push_back('/');
        }
        path.append(record.relativePath);
        return path;
    }

    const String& AssetSystem::GetAssetRoot() const
    {
        return m_assetRoot;
    }

    String AssetSystem::GetMetaPath(const AssetRecord& record) const
    {
        return MetaPathOf(record);
    }

    String AssetSystem::MetaPathOf(const AssetRecord& record) const
    {
        return AssetTypeRules::MakeMetaPath(SourcePathOf(record));
    }

    bool AssetSystem::ReadTexture(const AssetRecord& record, TextureData& data)
    {
        // 메타를 먼저 본다. 읽히지 않는 메타면 디코드는 헛일이다.
        if (false == ReadTextureOptions(record, data.options))
        {
            return false;
        }
        Array<std::byte> encoded;
        if (false == m_platform->ReadWholeFile(SourcePathOf(record).c_str(), encoded))
        {
            return false;
        }
        JArrayView<std::byte> view;
        view.data = encoded.Data();
        view.size = static_cast<std::uint32_t>(encoded.Size());
        DecodedImage image;
        if (false == DecodeImage(view, image))
        {
            return false;
        }
        data.width = image.width;
        data.height = image.height;
        data.pixels = std::move(image.pixels);
        // 프로젝트 기본은 여기서 한 번 적용한다(D-117). 그리는 쪽이 매번 프로젝트를 묻지 않게.
        data.filter = data.options.filter == TextureFilter::Default ? m_defaultTextureFilter : data.options.filter;
        return true;
    }

    bool AssetSystem::ReadMeta(const AssetRecord& record, AssetMetaFile& meta)
    {
        const AssetId key = record.owner.IsNull() ? record.id : record.owner;
        if (const AssetMetaFile* cached = m_metaCache.Find(key))
        {
            meta = *cached;
            return true;
        }
        AssetMetaError error;
        if (false == LoadAssetMetaFile(*m_platform, MetaPathOf(record).c_str(), meta, error))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: %s (line %zu)",
                MetaPathOf(record).c_str(), error.message.c_str(), error.line);
            return false;
        }
        m_metaCache.TryAdd(key, meta);
        return true;
    }

    void AssetSystem::ForgetMeta(AssetId id)
    {
        m_metaCache.Remove(id);
        if (const AssetRecord* record = m_registry != nullptr ? m_registry->Find(id) : nullptr)
        {
            if (false == record->owner.IsNull())
            {
                m_metaCache.Remove(record->owner);
            }
        }
    }

    // 옵션은 메타 파서 하나가 읽는다(D-120). 블록이 없으면 기본값이고, 있는데 읽히지 않으면 실패다 - 파서가 그 규칙이다.
    bool AssetSystem::ReadSpriteOptions(const AssetRecord& record, SpriteImportOptions& options)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        options = meta.hasSpriteOptions ? meta.spriteOptions : SpriteImportOptions{};
        // 0 이하·비유한 PPU 는 여기서 바로잡는다(D-119). 프레임 경로가 나누기 전에 값을 검사하지 않게.
        if (false == std::isfinite(options.pixelsPerUnit) || options.pixelsPerUnit <= 0.0f)
        {
            options.pixelsPerUnit = DefaultPixelsPerUnit;
        }
        return true;
    }

    bool AssetSystem::ReadTextureOptions(const AssetRecord& record, TextureImportOptions& options)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        options = meta.hasTextureOptions ? meta.textureOptions : TextureImportOptions{};
        return true;
    }

    bool AssetSystem::ReadAudio(const AssetRecord& record, AudioData& data)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        AudioData read;
        read.options = meta.hasAudioOptions ? meta.audioOptions : AudioImportOptions{};
        // 디스크 스트리밍은 헤더만 읽는다 - 파일이 메모리에 오지 않는다(D-203).
        if (read.options.mode == AudioImportMode::StreamFromDisk)
        {
            const String path = SourcePathOf(record);
            AudioFileDecoder decoder;
            if (false == decoder.Open(m_platform->OpenFileStream(path.c_str()), path.c_str()) || decoder.CountFrames() == 0)
            {
                Log::Write(LogLevel::Warning, "asset", "%s: this file cannot be streamed from disk", path.c_str());
                return false;
            }
            const AudioFormat format = decoder.GetFormat();
            read.sampleRate = format.sampleRate;
            read.channels = format.channels;
            read.frameCount = format.frameCount;
            read.streamPath = path;
            data = std::move(read);
            return true;
        }
        Array<std::byte> encoded;
        if (false == m_platform->ReadWholeFile(SourcePathOf(record).c_str(), encoded))
        {
            return false;
        }
        JArrayView<std::byte> view;
        view.data = encoded.Data();
        view.size = static_cast<std::uint32_t>(encoded.Size());
        AudioFormat format;
        const bool decoded = read.options.mode == AudioImportMode::Streaming
            ? ProbeAudio(view, format)
            : DecodeAudio(view, format, read.pcm);
        if (false == decoded)
        {
            Log::Write(LogLevel::Warning, "asset", "%s: not an audio file this engine can decode",
                SourcePathOf(record).c_str());
            return false;
        }
        if (read.options.mode == AudioImportMode::Streaming)
        {
            read.encoded = std::move(encoded);
        }
        read.sampleRate = format.sampleRate;
        read.channels = format.channels;
        read.frameCount = format.frameCount;
        data = std::move(read);
        return true;
    }

    void AssetSystem::NotifyAudioRelease(std::uint32_t slotIndex)
    {
        if (m_audioRelease == nullptr || slotIndex >= m_audio.slots.Size())
        {
            return;
        }
        const Slot<AudioData>& slot = m_audio.slots[slotIndex];
        if (slot.occupied)
        {
            m_audioRelease(m_audioReleaseUser, MakeHandle(AssetType::Audio, slotIndex, slot.generation));
        }
    }

    bool AssetSystem::ComputeAudioPeaks(AssetHandle handle, std::uint32_t buckets, Array<float>& peaks)
    {
        const AudioData* data = GetAudio(handle);
        if (data == nullptr)
        {
            return false;
        }
        if (false == data->pcm.IsEmpty())
        {
            JBro::ComputeAudioPeaks(data->pcm.Data(), data->frameCount, data->channels, buckets, peaks);
            return true;
        }
        if (false == data->encoded.IsEmpty())
        {
            JArrayView<std::byte> bytes;
            bytes.data = data->encoded.Data();
            bytes.size = static_cast<std::uint32_t>(data->encoded.Size());
            return JBro::ComputeAudioPeaks(bytes, buckets, peaks);
        }
        if (false == data->streamPath.empty() && m_platform != nullptr)
        {
            AudioFileDecoder decoder;
            return decoder.Open(m_platform->OpenFileStream(data->streamPath.c_str()), data->streamPath.c_str())
                && JBro::ComputeAudioPeaks(decoder, buckets, peaks);
        }
        return false;
    }

    void AssetSystem::SetAudioReleaseListener(AudioReleaseCallback callback, void* user)
    {
        m_audioRelease = callback;
        m_audioReleaseUser = callback != nullptr ? user : nullptr;
    }

    const AudioData* AssetSystem::GetAudio(AssetHandle handle) const
    {
        const Slot<AudioData>* slot = FindSlot(m_audio, handle, AssetType::Audio);
        return slot != nullptr ? &slot->data : nullptr;
    }

    void AssetSystem::SetDefaultTextureFilter(TextureFilter filter)
    {
        m_defaultTextureFilter = filter == TextureFilter::Default ? TextureFilter::Nearest : filter;
    }

    TextureFilter AssetSystem::GetDefaultTextureFilter() const
    {
        return m_defaultTextureFilter;
    }

    bool AssetSystem::BuildSprite(const AssetRecord& record, SpriteData& data)
    {
        SpriteData built;
        if (false == ReadSpriteOptions(record, built.options))
        {
            return false;
        }
        // 텍스처를 먼저 잡는다. 스프라이트가 사는 동안 텍스처는 내려가지 않는다.
        built.texture = Load(record.owner);
        const TextureData* texture = GetTexture(built.texture);
        if (texture == nullptr)
        {
            return false;
        }
        if (false == BuildSpriteFrames(texture->width, texture->height, built.options, built.frames))
        {
            Release(built.texture);
            return false;
        }
        data = std::move(built);
        return true;
    }

    AssetHandle AssetSystem::Load(AssetId id)
    {
        if (false == IsBound() || id.IsNull())
        {
            return {};
        }
        if (const AssetHandle* loaded = m_loaded.Find(id))
        {
            const AssetHandle handle = *loaded;
            switch (GetHandleType(handle))
            {
            case AssetType::Texture:
                if (Slot<TextureData>* slot = FindSlot(m_textures, handle, AssetType::Texture))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::Sprite:
                if (Slot<SpriteData>* slot = FindSlot(m_sprites, handle, AssetType::Sprite))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::Audio:
                if (Slot<AudioData>* slot = FindSlot(m_audio, handle, AssetType::Audio))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            default:
                break;
            }
            // 표에는 있는데 자리가 없다 - 어긋난 상태다. 빈 핸들로 두지 않고 새로 싣는다.
            m_loaded.Remove(id);
        }

        const AssetRecord* record = m_registry->Find(id);
        if (record == nullptr)
        {
            return {};
        }
        AssetHandle handle;
        switch (record->type)
        {
        case AssetType::Texture:
        {
            TextureData data;
            if (false == ReadTexture(*record, data))
            {
                return {};
            }
            handle = Occupy(m_textures, AssetType::Texture, id, std::move(data));
            break;
        }
        case AssetType::Sprite:
        {
            SpriteData data;
            if (false == BuildSprite(*record, data))
            {
                return {};
            }
            const AssetHandle texture = data.texture;
            handle = Occupy(m_sprites, AssetType::Sprite, id, std::move(data));
            if (handle.generation == 0)
            {
                // 풀이 찼다. 스프라이트가 잡은 텍스처를 놓아야 텍스처가 샌 채로 남지 않는다.
                Release(texture);
            }
            break;
        }
        case AssetType::Audio:
        {
            AudioData data;
            if (false == ReadAudio(*record, data))
            {
                return {};
            }
            handle = Occupy(m_audio, AssetType::Audio, id, std::move(data));
            break;
        }
        default:
            // 이 판이 아직 싣지 못하는 타입이다(asset-plan §3). 조용히 빈 핸들이다.
            return {};
        }
        if (handle.generation != 0)
        {
            m_loaded.TryAdd(id, handle);
        }
        return handle;
    }

    void AssetSystem::Release(AssetHandle handle)
    {
        if (Slot<TextureData>* texture = FindSlot(m_textures, handle, AssetType::Texture))
        {
            if (texture->referenceCount != 0)
            {
                --texture->referenceCount;
            }
            return;
        }
        if (Slot<SpriteData>* sprite = FindSlot(m_sprites, handle, AssetType::Sprite))
        {
            if (sprite->referenceCount != 0)
            {
                --sprite->referenceCount;
            }
            return;
        }
        if (Slot<AudioData>* audio = FindSlot(m_audio, handle, AssetType::Audio))
        {
            if (audio->referenceCount != 0)
            {
                --audio->referenceCount;
            }
        }
    }

    AssetHandle AssetSystem::Find(AssetId id) const
    {
        const AssetHandle* loaded = m_loaded.Find(id);
        return loaded != nullptr ? *loaded : AssetHandle{};
    }

    bool AssetSystem::IsLoaded(AssetHandle handle) const
    {
        return FindSlot(m_textures, handle, AssetType::Texture) != nullptr
            || FindSlot(m_sprites, handle, AssetType::Sprite) != nullptr
            || FindSlot(m_audio, handle, AssetType::Audio) != nullptr;
    }

    std::uint32_t AssetSystem::GetReferenceCount(AssetHandle handle) const
    {
        if (const Slot<TextureData>* texture = FindSlot(m_textures, handle, AssetType::Texture))
        {
            return texture->referenceCount;
        }
        if (const Slot<SpriteData>* sprite = FindSlot(m_sprites, handle, AssetType::Sprite))
        {
            return sprite->referenceCount;
        }
        if (const Slot<AudioData>* audio = FindSlot(m_audio, handle, AssetType::Audio))
        {
            return audio->referenceCount;
        }
        return 0;
    }

    const TextureData* AssetSystem::GetTexture(AssetHandle handle) const
    {
        const Slot<TextureData>* slot = FindSlot(m_textures, handle, AssetType::Texture);
        return slot != nullptr ? &slot->data : nullptr;
    }

    const SpriteData* AssetSystem::GetSprite(AssetHandle handle) const
    {
        const Slot<SpriteData>* slot = FindSlot(m_sprites, handle, AssetType::Sprite);
        return slot != nullptr ? &slot->data : nullptr;
    }

    bool AssetSystem::ReloadInPlace(AssetId id)
    {
        if (false == IsBound())
        {
            return false;
        }
        // 로드돼 있든 아니든 캐시한 메타는 버린다. 디스크가 바뀌었다는 뜻으로 불리는 함수다.
        ForgetMeta(id);
        const AssetHandle* loaded = m_loaded.Find(id);
        const AssetRecord* record = m_registry->Find(id);
        if (loaded == nullptr || record == nullptr)
        {
            return false;
        }
        if (Slot<TextureData>* texture = FindSlot(m_textures, *loaded, AssetType::Texture))
        {
            TextureData fresh;
            if (false == ReadTexture(*record, fresh))
            {
                return false;
            }
            fresh.pixelGeneration = texture->data.pixelGeneration + 1;
            texture->data = std::move(fresh);
            return true;
        }
        if (Slot<SpriteData>* sprite = FindSlot(m_sprites, *loaded, AssetType::Sprite))
        {
            SpriteImportOptions options;
            if (false == ReadSpriteOptions(*record, options))
            {
                return false;
            }
            const TextureData* texture = GetTexture(sprite->data.texture);
            if (texture == nullptr)
            {
                return false;
            }
            Array<SpriteFrame> frames;
            if (false == BuildSpriteFrames(texture->width, texture->height, options, frames))
            {
                return false;
            }
            sprite->data.options = options;
            sprite->data.frames = std::move(frames);
            return true;
        }
        if (Slot<AudioData>* audio = FindSlot(m_audio, *loaded, AssetType::Audio))
        {
            AudioData fresh;
            if (false == ReadAudio(*record, fresh))
            {
                return false;
            }
            // 새 자료를 다 읽은 뒤에 알린다 - 읽기가 실패하면 재생 중인 소리를 끊지 않는다.
            NotifyAudioRelease(loaded->index & SlotMask);
            fresh.dataGeneration = audio->data.dataGeneration + 1;
            audio->data = std::move(fresh);
            return true;
        }
        return false;
    }

    std::uint32_t AssetSystem::ReloadAllInPlace()
    {
        // 먼저 아이디를 모은다. 재로드는 표를 바꾸지 않지만, 도는 동안 표를 만지지 않는 쪽이 안전하다.
        Array<AssetId> ids;
        ids.Reserve(m_loaded.Size());
        for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it)
        {
            ids.Add(it->KeyValue);
        }
        std::uint32_t reloaded = 0;
        for (std::size_t index = 0; index < ids.Size(); ++index)
        {
            if (ReloadInPlace(ids[index]))
            {
                ++reloaded;
            }
        }
        return reloaded;
    }

    std::uint32_t AssetSystem::CollectUnused()
    {
        std::uint32_t freed = 0;
        // 스프라이트가 먼저다. 그것이 놓는 텍스처가 두 번째 순회에서 0 이 될 수 있다.
        for (std::uint32_t index = 0; index < m_sprites.slots.Size(); ++index)
        {
            Slot<SpriteData>& slot = m_sprites.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Release(slot.data.texture);
                Vacate(m_sprites, index);
                ++freed;
            }
        }
        for (std::uint32_t index = 0; index < m_textures.slots.Size(); ++index)
        {
            Slot<TextureData>& slot = m_textures.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_textures, index);
                ++freed;
            }
        }
        for (std::uint32_t index = 0; index < m_audio.slots.Size(); ++index)
        {
            Slot<AudioData>& slot = m_audio.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                NotifyAudioRelease(index);
                Vacate(m_audio, index);
                ++freed;
            }
        }
        return freed;
    }

    std::uint32_t AssetSystem::GetLoadedCount() const
    {
        return static_cast<std::uint32_t>(m_loaded.Size());
    }

    std::uint32_t AssetSystem::BindComponentAssets(const PropertyTable& table, void* component, Array<AssetHandle>& acquired)
    {
        if (component == nullptr)
        {
            return 0;
        }
        const NameId uuidName = NameTable::Get().Intern("JBro.Uuid");
        const NameId handleName = NameTable::Get().Intern("JBro.AssetHandle");
        std::uint32_t bound = 0;
        for (std::uint32_t i = 0; i < table.count; ++i)
        {
            const PropertyInfo& idProperty = table.properties[i];
            if (idProperty.type == nullptr || idProperty.type->typeName != uuidName)
            {
                continue;
            }
            const std::string_view idName = NameTable::Get().Resolve(idProperty.name);
            if (false == EndsWith(idName, "Id") || idName.size() <= 2)
            {
                continue;
            }
            const std::string_view handleFieldName = idName.substr(0, idName.size() - 2);
            for (std::uint32_t j = 0; j < table.count; ++j)
            {
                const PropertyInfo& handleProperty = table.properties[j];
                if (handleProperty.type == nullptr || handleProperty.type->typeName != handleName
                    || handleFieldName != NameTable::Get().Resolve(handleProperty.name))
                {
                    continue;
                }
                const AssetId id = *static_cast<const AssetId*>(idProperty.ConstAddress(component));
                AssetHandle& target = *static_cast<AssetHandle*>(handleProperty.Address(component));
                target = id.IsNull() ? AssetHandle{} : Load(id);
                if (target.generation != 0)
                {
                    acquired.Add(target);
                    ++bound;
                }
                break;
            }
        }
        return bound;
    }

    void AssetSystem::ReleaseAll(Array<AssetHandle>& acquired)
    {
        for (std::size_t index = 0; index < acquired.Size(); ++index)
        {
            Release(acquired[index]);
        }
        acquired.Clear();
    }
}
