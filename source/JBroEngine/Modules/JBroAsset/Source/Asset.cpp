#include <JBro/Core/Log.h>
#include <JBro/Asset/Asset.h>

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Asset/AudioDecoder.h>
#include <JBro/Asset/AudioEffectFile.h>
#include <JBro/Asset/ColorLutFile.h>
#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Asset/ModelImport.h>
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
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro
{
    namespace
    {
        // 핸들 번호의 위 8 비트가 타입, 아래 24 비트가 슬롯이다(D-322 (2)). `AssetSystem::TypeShift` 와 같아야 한다 - `GetType` 이 잰다.
        constexpr std::uint32_t HandleTypeShift = 24;

        constexpr AssetHandle MakeHandle(AssetType type, UInt32 slotIndex, UInt32 generation) noexcept
        {
            AssetHandle handle;
            handle.index = (static_cast<JBro::UInt32>(type) << HandleTypeShift) | slotIndex;
            handle.generation = generation;
            return handle;
        }

        Bool EndsWith(std::string_view text, std::string_view suffix) noexcept
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // 컴포넌트의 `xxxId`(`AssetId`) 필드와 그 짝 `xxx`(`AssetHandle`) 필드를 찾아 짝마다 `visit` 를 부른다. 해석 패스와
        // 아이디 모으기가 같은 규칙을 쓰도록 한 곳에 둔다.
        template <typename Visitor>
        void ForEachAssetField(const PropertyTable& table, Visitor&& visit)
        {
            const NameId uuidName = NameTable::Get().Intern("JBro.Uuid");
            const NameId handleName = NameTable::Get().Intern("JBro.AssetHandle");
            for (UInt32 i = 0; i < table.count; ++i)
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
                for (UInt32 j = 0; j < table.count; ++j)
                {
                    const PropertyInfo& handleProperty = table.properties[j];
                    if (handleProperty.type == nullptr || handleProperty.type->typeName != handleName
                        || handleFieldName != NameTable::Get().Resolve(handleProperty.name))
                    {
                        continue;
                    }
                    visit(idProperty, handleProperty);
                    break;
                }
            }
        }

        // 목록 짝이다(D-306): `xxxIds`(`Array<AssetId>`)와 `xxxs`(`Array<AssetHandle>`). `AudioSource.audioEffectIds` 처럼 차례가 뜻을
        // 갖는 에셋 목록이 이 짝으로 풀린다. 원소는 타입 소거 조작(`ArrayOps`)으로 다룬다.
        template <typename Visitor>
        void ForEachAssetListField(const PropertyTable& table, Visitor&& visit)
        {
            const NameId uuidName = NameTable::Get().Intern("JBro.Uuid");
            const NameId handleName = NameTable::Get().Intern("JBro.AssetHandle");
            for (UInt32 i = 0; i < table.count; ++i)
            {
                const PropertyInfo& idProperty = table.properties[i];
                if (idProperty.type == nullptr || idProperty.type->arrayOps == nullptr || idProperty.type->element == nullptr
                    || idProperty.type->element->typeName != uuidName)
                {
                    continue;
                }
                const std::string_view idName = NameTable::Get().Resolve(idProperty.name);
                if (false == EndsWith(idName, "Ids") || idName.size() <= 3)
                {
                    continue;
                }
                // `audioEffectIds` → `audioEffects`
                char handleFieldName[64] = {};
                const std::size_t stem = idName.size() - 3;
                if (stem + 2 > sizeof(handleFieldName))
                {
                    continue;
                }
                std::memcpy(handleFieldName, idName.data(), stem);
                handleFieldName[stem] = 's';
                for (UInt32 j = 0; j < table.count; ++j)
                {
                    const PropertyInfo& handleProperty = table.properties[j];
                    if (handleProperty.type == nullptr || handleProperty.type->arrayOps == nullptr
                        || handleProperty.type->element == nullptr || handleProperty.type->element->typeName != handleName
                        || std::string_view(handleFieldName) != NameTable::Get().Resolve(handleProperty.name))
                    {
                        continue;
                    }
                    visit(idProperty, handleProperty);
                    break;
                }
            }
        }
    }

    Bool AssetSystem::Initialize(const JMemoryContext&)
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
        m_looseSource.Bind(&platform, m_assetRoot.c_str());
        m_source = &m_looseSource;
    }

    void AssetSystem::Bind(IPlatform& platform, const AssetRegistry& registry, const IAssetSource& source)
    {
        Unbind();
        m_platform = &platform;
        m_registry = &registry;
        m_source = &source;
    }

    const IAssetSource* AssetSystem::GetSource() const
    {
        return m_source;
    }

    Bool AssetSystem::ReadSourceByPath(std::string_view relativePath, Array<std::byte>& out) const
    {
        out.Clear();
        const AssetRecord* record = m_registry != nullptr ? m_registry->FindByPath(relativePath) : nullptr;
        return record != nullptr && m_source != nullptr && m_source->Read(*record, AssetBlob::Source, out);
    }

    void AssetSystem::Unbind()
    {
        // 믹서가 빌려 쓰는 오디오 자료를 풀기 전에 알린다.
        for (UInt32 index = 0; index < m_audio.slots.Size(); ++index)
        {
            NotifyAudioRelease(index);
        }
        m_audio = {};
        m_fontFamilies = {};
        m_stringTables = {};
        m_audioEffects = {};
        m_colorLuts = {};
        m_environmentMaps = {};
        m_postProcessProfiles = {};
        m_shaders = {};
        m_materials = {};
        m_meshes = {};
        m_skeletons = {};
        m_animationClips = {};
        m_fonts = {};
        m_textures = {};
        m_sprites = {};
        m_loaded.Clear();
        m_metaCache.Clear();
        m_platform = nullptr;
        m_registry = nullptr;
        m_assetRoot.clear();
        m_looseSource.Bind(nullptr, nullptr);
        m_source = nullptr;
        // 워커에서 도는 미룬 작업의 결과는 버린다(D-455).
        ++m_bindSerial;
        m_decodedModelId = {};
        m_decodedModel = {};
    }

    Bool AssetSystem::IsBound() const
    {
        return m_platform != nullptr && m_registry != nullptr;
    }

    AssetType AssetSystem::GetHandleType(AssetHandle handle)
    {
        if (handle.generation == 0)
        {
            return AssetType::Unknown;
        }
        static_assert(TypeShift.Get() == HandleTypeShift, "the handle packing and AssetSystem::TypeShift must agree");
        return static_cast<AssetType>(handle.index >> TypeShift);
    }

    template <typename TData>
    AssetSystem::Slot<TData>* AssetSystem::FindSlot(Pool<TData>& pool, AssetHandle handle, AssetType type)
    {
        if (handle.generation == 0 || GetHandleType(handle) != type)
        {
            return nullptr;
        }
        const UInt32 slotIndex = handle.index & SlotMask;
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
        UInt32 slotIndex = 0;
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
            slotIndex = static_cast<JBro::UInt32>(pool.slots.Size());
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
    void AssetSystem::Vacate(Pool<TData>& pool, UInt32 slotIndex)
    {
        Slot<TData>& slot = pool.slots[slotIndex];
        m_loaded.Remove(slot.id);
        // 내려간 것의 메타는 잊는다. 다음 로드는 디스크를 본다 - 손으로 고친 `.jmeta` 는 감시가 무시하므로 여기가 그 길이다.
        ForgetMeta(slot.id);
        slot.data = TData{};
        slot.id = {};
        slot.referenceCount = 0;
        slot.occupied = false;
        slot.pending = false;
        slot.queued = false;
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

    String AssetSystem::GetSourcePath(const AssetRecord& record) const
    {
        return SourcePathOf(record);
    }

    String AssetSystem::MetaPathOf(const AssetRecord& record) const
    {
        return AssetTypeRules::MakeMetaPath(SourcePathOf(record));
    }

    Bool AssetSystem::ReadTexture(const AssetRecord& record, TextureData& data)
    {
        // 메타를 먼저 본다. 읽히지 않는 메타면 디코드는 헛일이다.
        if (false == ReadTextureOptions(record, data.options))
        {
            return false;
        }
        data.sharedImageKey = record.sharedImageKey;
        // 같은 그림이 이미 실렸으면 그 픽셀이다 - 모델을 다시 읽지도 블롭을 다시 풀지도 않는다(material-plan 5 단계).
        if (CopySharedImage(record, data))
        {
            data.filter = data.options.filter == TextureFilter::Default ? m_defaultTextureFilter : data.options.filter;
            return true;
        }
        // 빌드가 디코드해 둔 픽셀이 있으면 그것이다(D-232) - 게임에 이미지 디코더가 돌지 않는다. 모델의 내장 그림도 빌드가 꺼내 둔다(6 단계).
        // 같은 그림은 빌드가 한 번만 구워 두므로 제 블롭이 없으면 같은 열쇠의 것을 읽는다.
        if (const AssetRecord* cookedFrom = CookedImageOf(record))
        {
            Array<std::byte> cooked;
            CookedTextureInfo info;
            if (false == m_source->Read(*cookedFrom, AssetBlob::CookedTexture, cooked) || false == ReadCookedTexture(cooked, info))
            {
                Log::Write(LogLevel::Warning, "asset", "%s: the cooked texture is damaged", record.relativePath.c_str());
                return false;
            }
            data.width = info.width;
            data.height = info.height;
            data.pixels.Resize(static_cast<std::size_t>(info.width) * info.height * 4);
            std::memcpy(data.pixels.Data(), cooked.Data() + CookedTextureHeaderSize, data.pixels.Size());
            data.filter = data.options.filter == TextureFilter::Default ? m_defaultTextureFilter : data.options.filter;
            return true;
        }
        // 모델의 내장 그림은 모델 안의 바이트다(5 단계).
        if (const AssetRecord* model = OwnerModelOf(record))
        {
            return ReadEmbeddedTexture(record, *model, data);
        }
        // 원본은 워커 로드와 같은 디코드를 부른다(D-236). 동기 로드는 메인에서 부를 뿐이다.
        AssetDecodeJob job;
        job.type = AssetType::Texture;
        job.record = record;
        if (false == DecodeAssetFile(*m_source, job))
        {
            return false;
        }
        data.width = job.texture.width;
        data.height = job.texture.height;
        data.pixels = std::move(job.texture.pixels);
        // 프로젝트 기본은 여기서 한 번 적용한다(D-117). 그리는 쪽이 매번 프로젝트를 묻지 않게.
        data.filter = data.options.filter == TextureFilter::Default ? m_defaultTextureFilter : data.options.filter;
        return true;
    }

    Bool DecodeAssetFile(const IAssetSource& source, AssetDecodeJob& job)
    {
        job.decoded = false;
        job.failure.clear();
        const String& name = job.record.relativePath;
        if (job.type == AssetType::Texture)
        {
            Array<std::byte> encoded;
            if (false == source.Read(job.record, AssetBlob::Source, encoded))
            {
                job.failure = "cannot read " + name;
                return false;
            }
            JArrayView<std::byte> view;
            view.data = encoded.Data();
            view.size = static_cast<JBro::UInt32>(encoded.Size());
            DecodedImage image;
            if (false == DecodeImage(view, image))
            {
                job.failure = "not an image this engine can decode: " + name;
                return false;
            }
            job.texture.width = image.width;
            job.texture.height = image.height;
            job.texture.pixels = std::move(image.pixels);
            job.decoded = true;
            return true;
        }
        if (job.type == AssetType::Audio)
        {
            AudioData& read = job.audio;
            // 디스크 스트리밍은 헤더만 읽는다 - 파일이 메모리에 오지 않는다(D-203). `OpenStream` 은 어느 스레드에서 불러도 된다.
            if (read.options.mode == AudioImportMode::StreamFromDisk)
            {
                AudioFileDecoder decoder;
                if (false == decoder.Open(source.OpenStream(job.streamPath.c_str()), job.streamPath.c_str())
                    || decoder.CountFrames() == 0)
                {
                    job.failure = job.streamPath + ": this file cannot be streamed from disk";
                    return false;
                }
                const AudioFormat format = decoder.GetFormat();
                read.sampleRate = format.sampleRate;
                // 모노면 믹서의 스트리머가 연 디코더에 `SetMono` 를 건다 - 클립의 채널 1 이 그 신호다(D-231).
                read.channels = read.options.mono ? UInt32(1) : format.channels;
                read.frameCount = format.frameCount;
                read.streamPath = job.streamPath;
                job.decoded = true;
                return true;
            }
            Array<std::byte> encoded;
            if (false == source.Read(job.record, AssetBlob::Source, encoded))
            {
                job.failure = "cannot read " + name;
                return false;
            }
            JArrayView<std::byte> view;
            view.data = encoded.Data();
            view.size = static_cast<JBro::UInt32>(encoded.Size());
            AudioFormat format;
            AudioDecodeTarget target;
            target.sampleRate = job.audioSampleRate;
            target.mono = read.options.mono;
            const Bool decoded = read.options.mode == AudioImportMode::Streaming
                ? ProbeAudio(view, format)
                : DecodeAudio(view, format, read.pcm, target);
            if (false == decoded)
            {
                job.failure = name + ": not an audio file this engine can decode";
                return false;
            }
            if (read.options.mode == AudioImportMode::Streaming)
            {
                read.encoded = std::move(encoded);
            }
            read.sampleRate = format.sampleRate;
            // `Streaming` 은 보이스마다 여는 디코더가 클립의 채널로 푼다 - 모노면 1 로 알린다(D-231).
            read.channels = read.options.mono ? UInt32(1) : format.channels;
            read.frameCount = format.frameCount;
            job.decoded = true;
            return true;
        }
        if (job.type == AssetType::EnvironmentMap)
        {
            Array<std::byte> bytes;
            if (false == source.Read(job.record, AssetBlob::Source, bytes))
            {
                job.failure = "cannot read " + name;
                return false;
            }
            DecodedHdrImage image;
            if (false == DecodeHdrImage({bytes.Data(), static_cast<JBro::UInt32>(bytes.Size())}, image))
            {
                job.failure = "a sky map could not be read (" + name + "): only Radiance .hdr pictures are skies";
                return false;
            }
            job.environment.width = image.width;
            job.environment.height = image.height;
            job.environment.rgb = std::move(image.rgb);
            job.decoded = true;
            return true;
        }
        if (job.type == AssetType::Model)
        {
            // 모델 하나를 통째로 디코드하고(MikkTSpace 탄젠트까지), 기다리는 내장 그림을 푼다. 메시·재질은 받는 쪽이 디코드한 모델에서 옮긴다.
            Array<std::byte> bytes;
            if (false == source.Read(job.record, AssetBlob::Source, bytes))
            {
                job.failure = name + ": the model file could not be read";
                return false;
            }
            struct SideReader
            {
                IPlatform* platform;
                String folder;
                static Bool Read(void* user, const char* relativePath, Array<std::byte>& out)
                {
                    const SideReader& self = *static_cast<const SideReader*>(user);
                    out.Clear();
                    if (self.platform == nullptr || self.folder.empty())
                    {
                        return false;
                    }
                    String path = self.folder;
                    path += relativePath;
                    return self.platform->ReadWholeFile(path.c_str(), out);
                }
            };
            SideReader side{job.platform, String()};
            if (false == job.assetRoot.empty())
            {
                side.folder = job.assetRoot;
                if (side.folder.back() != '/' && side.folder.back() != '\\')
                {
                    side.folder += "/";
                }
                const std::size_t slash = name.find_last_of('/');
                if (slash != String::npos)
                {
                    side.folder += String(name.substr(0, slash + 1));
                }
            }
            ModelFileReader reader;
            reader.read = &SideReader::Read;
            reader.user = &side;
            String error;
            if (false == DecodeModel({bytes.Data(), static_cast<UInt32>(bytes.Size())}, ModelFormatFromPath(name), job.modelOptions, reader, job.model, error))
            {
                job.failure = name + ": " + error;
                return false;
            }
            job.embedded.Resize(job.targets.Size());
            Array<ModelSubAsset> listed;
            ListModelSubAssets(job.model, listed);
            for (std::size_t at = 0; at < job.targets.Size(); ++at)
            {
                if (job.targets[at].embeddedName.empty())
                {
                    continue;
                }
                const ModelSubAsset* sub = FindModelSubAsset(listed, AssetType::Texture, job.targets[at].embeddedName);
                if (sub == nullptr)
                {
                    continue;
                }
                const ModelImage& image = job.model.images[sub->index.Get()];
                DecodedImage pixels;
                if (DecodeImage({image.embedded.Data(), static_cast<JBro::UInt32>(image.embedded.Size())}, pixels))
                {
                    job.embedded[at].width = pixels.width;
                    job.embedded[at].height = pixels.height;
                    job.embedded[at].pixels = std::move(pixels.pixels);
                }
            }
            job.decoded = true;
            return true;
        }
        job.failure = "this asset type is not decoded on workers: " + name;
        return false;
    }

    Bool AssetSystem::ReadMeta(const AssetRecord& record, AssetMetaFile& meta)
    {
        const AssetId key = record.owner.IsNull() ? record.id : record.owner;
        if (const AssetMetaFile* cached = m_metaCache.Find(key))
        {
            meta = *cached;
            return true;
        }
        AssetMetaError error;
        // 이미지의 Sprite 는 Texture 의 메타를 쓴다. 패키지에는 주인의 메타만 있다.
        const AssetRecord* owner = record.owner.IsNull() || m_registry == nullptr ? nullptr : m_registry->Find(record.owner);
        const AssetRecord& metaRecord = owner != nullptr ? *owner : record;
        Array<std::byte> text;
        if (false == m_source->Read(metaRecord, AssetBlob::Meta, text))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the meta file could not be read", metaRecord.relativePath.c_str());
            return false;
        }
        if (false == ParseAssetMetaFile(reinterpret_cast<const char*>(text.Data()), text.Size(), meta, error))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: %s (line %zu)",
                metaRecord.relativePath.c_str(), error.message.c_str(), error.line);
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
    Bool AssetSystem::ReadSpriteOptions(const AssetRecord& record, SpriteImportOptions& options)
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

    Bool AssetSystem::ReadTextureOptions(const AssetRecord& record, TextureImportOptions& options)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        // 모델의 내장 그림은 하위 목록의 제 항목이 옵션을 든다. 항목에 옵션이 없으면 모델 그림의 기본이다.
        if (meta.type == AssetType::Model)
        {
            options = ModelTextureDefaults();
            for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
            {
                if (meta.subAssets[index].id == record.id && meta.subAssets[index].hasTextureOptions)
                {
                    options = meta.subAssets[index].textureOptions;
                }
            }
            return true;
        }
        options = meta.hasTextureOptions ? meta.textureOptions : TextureImportOptions{};
        return true;
    }

    Bool AssetSystem::ReadAudio(const AssetRecord& record, AudioData& data)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        // 워커 로드와 같은 디코드를 부른다(D-236).
        AssetDecodeJob job;
        job.type = AssetType::Audio;
        job.record = record;
        job.audio.options = meta.hasAudioOptions ? meta.audioOptions : AudioImportOptions{};
        job.audioSampleRate = m_audioDecodeSampleRate;
        if (job.audio.options.mode == AudioImportMode::StreamFromDisk)
        {
            job.streamPath = m_source->MakeStreamPath(record);
        }
        if (false == DecodeAssetFile(*m_source, job))
        {
            // 파일이 없는 것은 조용히 둔다(전과 같다). 풀지 못한 것만 말한다.
            if (job.failure.rfind("cannot read ", 0) != 0)
            {
                Log::Write(LogLevel::Warning, "asset", "%s", job.failure.c_str());
            }
            return false;
        }
        data = std::move(job.audio);
        return true;
    }

    Bool AssetSystem::PrepareDecode(AssetId id, AssetDecodeJob& job)
    {
        if (false == IsBound() || id.IsNull() || m_source == nullptr || false == m_source->CanReadOnWorkers())
        {
            return false;
        }
        const AssetRecord* record = m_registry->Find(id);
        if (record == nullptr)
        {
            return false;
        }
        // 스프라이트는 주인 텍스처를 대신 보낸다. 스프라이트 자체는 바인딩 때 프레임만 짓는다.
        if (record->type == AssetType::Sprite)
        {
            record = m_registry->Find(record->owner);
            if (record == nullptr)
            {
                return false;
            }
        }
        if (record->type != AssetType::Texture && record->type != AssetType::Audio)
        {
            return false;
        }
        // 모델의 내장 그림은 모델을 읽어야 나온다 - 들고 있는 모델이 메인 스레드의 것이라 동기 로드에 둔다.
        if (OwnerModelOf(*record) != nullptr)
        {
            return false;
        }
        if (m_loaded.Find(record->id) != nullptr)
        {
            return false;
        }
        // 빌드가 디코드해 둔 텍스처는 복사뿐이다. 동기 로드에 둔다.
        if (record->type == AssetType::Texture && m_source->Has(*record, AssetBlob::CookedTexture))
        {
            return false;
        }
        job = AssetDecodeJob{};
        job.id = record->id;
        job.type = record->type;
        job.record = *record;
        if (record->type == AssetType::Texture)
        {
            return ReadTextureOptions(*record, job.texture.options);
        }
        AssetMetaFile meta;
        if (false == ReadMeta(*record, meta))
        {
            return false;
        }
        job.audio.options = meta.hasAudioOptions ? meta.audioOptions : AudioImportOptions{};
        job.audioSampleRate = m_audioDecodeSampleRate;
        if (job.audio.options.mode == AudioImportMode::StreamFromDisk)
        {
            job.streamPath = m_source->MakeStreamPath(*record);
        }
        return true;
    }

    AssetHandle AssetSystem::AdoptDecoded(AssetDecodeJob& job)
    {
        if (false == job.targets.IsEmpty())
        {
            AdoptDeferred(job);
            return {};
        }
        if (false == IsBound() || job.id.IsNull())
        {
            return {};
        }
        if (false == job.decoded)
        {
            if (false == job.failure.empty())
            {
                Log::Write(LogLevel::Warning, "asset", "%s", job.failure.c_str());
            }
            return {};
        }
        // 워커가 도는 사이 동기 로드가 먼저 실었다. 그 핸들에 참조를 더하고 디코드한 것은 버린다.
        if (m_loaded.Find(job.id) != nullptr)
        {
            return Load(job.id);
        }
        AssetHandle handle;
        if (job.type == AssetType::Texture)
        {
            TextureData& data = job.texture;
            data.filter = data.options.filter == TextureFilter::Default ? m_defaultTextureFilter : data.options.filter;
            handle = Occupy(m_textures, AssetType::Texture, job.id, std::move(data));
        }
        else if (job.type == AssetType::Audio)
        {
            handle = Occupy(m_audio, AssetType::Audio, job.id, std::move(job.audio));
        }
        if (handle.generation != 0)
        {
            m_loaded.TryAdd(job.id, handle);
        }
        return handle;
    }

    Bool AssetSystem::ReadFont(const AssetRecord& record, FontData& data)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        FontData read;
        read.options = meta.hasFontOptions ? meta.fontOptions : FontImportOptions{};
        NormalizeFontOptions(read.options, m_defaultTextureFilter);
        if (false == m_source->Read(record, AssetBlob::Source, read.bytes))
        {
            return false;
        }
        // 미리 뜬 아틀라스는 있으면 함께 든다. 읽지 못해도 폰트는 선다 - 라이브러리가 지금처럼 뜬다.
        if (m_source->Has(record, AssetBlob::FontAtlas) && false == m_source->Read(record, AssetBlob::FontAtlas, read.bakedAtlas))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the baked atlas is damaged; the font will prewarm at run time",
                record.relativePath.c_str());
            read.bakedAtlas.Clear();
        }
        // 바이트가 폰트인지는 여기서 보지 않는다 - 에셋 모듈은 텍스트 커널을 모른다. 여는 것은 텍스트 시스템이고,
        // 열지 못하면 그쪽이 경고하고 그리지 않는다.
        data = std::move(read);
        return true;
    }

    void NormalizeFontOptions(FontImportOptions& options, TextureFilter projectDefault)
    {
        // 텍스처와 같은 규칙으로 여기서 정해 둔다(D-119). 쓰는 쪽은 `Default`·0 이하를 보지 않는다.
        if (options.filter == TextureFilter::Default)
        {
            options.filter = projectDefault == TextureFilter::Default ? TextureFilter::Nearest : projectDefault;
        }
        if (false == (options.pixelsPerUnit > 0.0f))
        {
            options.pixelsPerUnit = DefaultPixelsPerUnit;
        }
        // 거리장은 이웃 텍셀을 섞어야 가장자리가 선다. Nearest 로 읽으면 계단이 된다.
        if (options.renderMode == FontRenderMode::Sdf)
        {
            options.filter = TextureFilter::Linear;
        }
        options.sdfSize = JBro::Clamp(options.sdfSize, 8u, 256u);
        options.sdfSpread = JBro::Clamp(options.sdfSpread, 1u, 32u);
    }

    Bool AssetSystem::ReadFontFamily(const AssetRecord& record, FontFamilyData& data)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        FontFamilyData read;
        read.options = meta.hasFontFamilyOptions ? meta.fontFamilyOptions : FontFamilyOptions{};
        const AssetId ids[] = { read.options.regularFontId, read.options.boldFontId, read.options.italicFontId,
            read.options.boldItalicFontId };
        for (std::size_t slot = 0; slot < static_cast<std::size_t>(FontFamilySlot::Count); ++slot)
        {
            if (ids[slot].IsNull() || ids[slot] == record.id)
            {
                continue;
            }
            const AssetHandle font = Load(ids[slot]);
            // 패밀리의 칸은 Font 만이다. 다른 타입(패밀리 안의 패밀리 포함)을 가리키면 놓고 비운다.
            if (font.generation != 0 && GetHandleType(font) != AssetType::Font)
            {
                Release(font);
                continue;
            }
            read.fonts[slot] = font;
        }
        data = std::move(read);
        return true;
    }

    void AssetSystem::ReleaseFamilyFonts(FontFamilyData& data)
    {
        for (AssetHandle& font : data.fonts)
        {
            if (font.generation != 0)
            {
                Release(font);
            }
            font = {};
        }
    }

    Bool AssetSystem::ReadStringTable(const AssetRecord& record, StringTableData& data)
    {
        AssetMetaFile meta;
        if (false == ReadMeta(record, meta))
        {
            return false;
        }
        Array<std::byte> bytes;
        if (false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        StringTableData read;
        read.options = meta.hasStringTableOptions ? meta.stringTableOptions : StringTableOptions{};
        YamlDocument document;
        YamlError error;
        if (false == document.Parse(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), error))
        {
            Log::Write(LogLevel::Warning, "asset", "a string table could not be read (line %zu): %s", error.line, error.message.c_str());
            return false;
        }
        const UInt32 root = document.GetRoot();
        if (document.GetKind(root) == YamlKind::Map)
        {
            for (std::size_t index = 0; index < document.GetCount(root); ++index)
            {
                const UInt32 value = document.GetValue(root, index);
                if (document.GetKind(value) != YamlKind::Scalar)
                {
                    continue;
                }
                read.entries.FindOrAdd(String(document.GetKey(root, index))) = String(document.GetText(value));
            }
        }
        data = std::move(read);
        return true;
    }

    Bool AssetSystem::ReadAudioEffect(const AssetRecord& record, AudioEffectData& data)
    {
        Array<std::byte> bytes;
        if (m_source == nullptr || false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        AudioEffectData read;
        String error;
        if (false == ParseAudioEffect(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), read.settings, &error))
        {
            Log::Write(LogLevel::Warning, "asset", "a sound effect could not be read (%s): %s", record.relativePath.c_str(), error.c_str());
            return false;
        }
        data = read;
        return true;
    }

    const AudioEffectData* AssetSystem::GetAudioEffect(AssetHandle handle) const
    {
        const Slot<AudioEffectData>* slot = FindSlot(m_audioEffects, handle, AssetType::AudioEffect);
        return slot != nullptr ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReadColorLut(const AssetRecord& record, ColorLutData& data)
    {
        Array<std::byte> bytes;
        if (m_source == nullptr || false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        ColorLutData read;
        String error;
        if (false == ParseColorLut(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), read.lut, &error))
        {
            Log::Write(LogLevel::Warning, "asset", "a colour LUT could not be read (%s): %s", record.relativePath.c_str(), error.c_str());
            return false;
        }
        data = std::move(read);
        return true;
    }

    const ColorLutData* AssetSystem::GetColorLut(AssetHandle handle) const
    {
        const Slot<ColorLutData>* slot = FindSlot(m_colorLuts, handle, AssetType::ColorLut);
        return slot != nullptr ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReadEnvironmentMap(const AssetRecord& record, EnvironmentMapData& data)
    {
        Array<std::byte> bytes;
        if (m_source == nullptr || false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        DecodedHdrImage image;
        if (false == DecodeHdrImage({bytes.Data(), static_cast<JBro::UInt32>(bytes.Size())}, image))
        {
            Log::Write(LogLevel::Warning, "asset", "a sky map could not be read (%s): only Radiance .hdr pictures are skies", record.relativePath.c_str());
            return false;
        }
        EnvironmentMapData read;
        read.width = image.width;
        read.height = image.height;
        read.rgb = std::move(image.rgb);
        data = std::move(read);
        return true;
    }

    const EnvironmentMapData* AssetSystem::GetEnvironmentMap(AssetHandle handle) const
    {
        const Slot<EnvironmentMapData>* slot = FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap);
        return slot != nullptr && false == slot->pending ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReadPostProcessProfile(const AssetRecord& record, PostProcessProfileData& data)
    {
        Array<std::byte> bytes;
        if (m_source == nullptr || false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        data.text.assign(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        return true;
    }

    const PostProcessProfileData* AssetSystem::GetPostProcessProfile(AssetHandle handle) const
    {
        const Slot<PostProcessProfileData>* slot = FindSlot(m_postProcessProfiles, handle, AssetType::PostProcessProfile);
        return slot != nullptr ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReadRenderTexture(const AssetRecord& record, RenderTextureData& data)
    {
        Array<std::byte> bytes;
        if (m_source == nullptr || false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        RenderTextureData read;
        String error;
        if (false == ParseRenderTexture(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), read.settings, &error))
        {
            Log::Write(LogLevel::Warning, "asset", "a render texture could not be read (%s): %s", record.relativePath.c_str(), error.c_str());
            return false;
        }
        data = read;
        return true;
    }

    const RenderTextureData* AssetSystem::GetRenderTexture(AssetHandle handle) const
    {
        const Slot<RenderTextureData>* slot = FindSlot(m_renderTextures, handle, AssetType::RenderTexture);
        return slot != nullptr ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReadShader(const AssetRecord& record, ShaderAssetData& data)
    {
        if (m_source == nullptr)
        {
            return false;
        }
        data.path = record.relativePath;
        // 패키지는 구운 묶음만 든다(D-347). 느슨한 폴더는 원본만 든다.
        if (m_source->Has(record, AssetBlob::CompiledShader))
        {
            Array<std::byte> compiled;
            if (false == m_source->Read(record, AssetBlob::CompiledShader, compiled)
                || false == ReadCompiledShaderSet(compiled.Data(), compiled.Size(), data.compiled))
            {
                Log::Write(LogLevel::Warning, "asset", "a compiled shader could not be read (%s)", record.relativePath.c_str());
                return false;
            }
            data.hasCompiled = true;
            return true;
        }
        Array<std::byte> bytes;
        if (false == m_source->Read(record, AssetBlob::Source, bytes))
        {
            return false;
        }
        data.source.assign(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        return true;
    }

    const ShaderAssetData* AssetSystem::GetShader(AssetHandle handle) const
    {
        const Slot<ShaderAssetData>* slot = FindSlot(m_shaders, handle, AssetType::Shader);
        return slot != nullptr ? &slot->data : nullptr;
    }

    const MeshData* AssetSystem::GetMesh(AssetHandle handle) const
    {
        const Slot<MeshData>* slot = FindSlot(m_meshes, handle, AssetType::Mesh);
        return slot != nullptr && false == slot->pending ? &slot->data : nullptr;
    }

    const SkeletonData* AssetSystem::GetSkeleton(AssetHandle handle) const
    {
        const Slot<SkeletonData>* slot = FindSlot(m_skeletons, handle, AssetType::Skeleton);
        return slot != nullptr && false == slot->pending ? &slot->data : nullptr;
    }

    const AnimationClipData* AssetSystem::GetAnimationClip(AssetHandle handle) const
    {
        const Slot<AnimationClipData>* slot = FindSlot(m_animationClips, handle, AssetType::AnimationClip);
        return slot != nullptr && false == slot->pending ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReadSkeleton(const AssetRecord& record, SkeletonData& data)
    {
        const AssetRecord* model = OwnerModelOf(record);
        const ModelData* decoded = model != nullptr ? DecodeOwnerModel(*model) : nullptr;
        if (decoded == nullptr)
        {
            return false;
        }
        Array<ModelSubAsset> listed;
        ListModelSubAssets(*decoded, listed);
        const ModelSubAsset* sub = FindModelSubAsset(listed, AssetType::Skeleton, record.name);
        if (sub == nullptr)
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the model has no skeleton named %s", model->relativePath.c_str(), record.name.c_str());
            return false;
        }
        data.skeleton = decoded->skeletons[sub->index.Get()];
        return true;
    }

    Bool AssetSystem::ReadAnimationClip(const AssetRecord& record, AnimationClipData& data)
    {
        const AssetRecord* model = OwnerModelOf(record);
        const ModelData* decoded = model != nullptr ? DecodeOwnerModel(*model) : nullptr;
        if (decoded == nullptr)
        {
            return false;
        }
        Array<ModelSubAsset> listed;
        ListModelSubAssets(*decoded, listed);
        const ModelSubAsset* sub = FindModelSubAsset(listed, AssetType::AnimationClip, record.name);
        if (sub == nullptr)
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the model has no animation named %s", model->relativePath.c_str(), record.name.c_str());
            return false;
        }
        data.animation = decoded->animations[sub->index.Get()];
        return true;
    }

    Bool AssetSystem::ReadLooseFile(std::string_view relativePath, Array<std::byte>& out) const
    {
        out.Clear();
        if (m_platform == nullptr || m_assetRoot.empty())
        {
            return false;
        }
        String path = m_assetRoot;
        if (path.back() != '/' && path.back() != '\\')
        {
            path += "/";
        }
        path += String(relativePath);
        return m_platform->ReadWholeFile(path.c_str(), out);
    }

    const AssetRecord* AssetSystem::OwnerModelOf(const AssetRecord& record) const
    {
        const AssetRecord* owner = record.owner.IsNull() || m_registry == nullptr ? nullptr : m_registry->Find(record.owner);
        return owner != nullptr && owner->type == AssetType::Model ? owner : nullptr;
    }

    const ModelData* AssetSystem::DecodeOwnerModel(const AssetRecord& model)
    {
        if (m_source == nullptr)
        {
            return nullptr;
        }
        if (m_decodedModelId == model.id)
        {
            return &m_decodedModel;
        }
        AssetMetaFile meta;
        if (false == ReadMeta(model, meta))
        {
            return nullptr;
        }
        Array<std::byte> bytes;
        if (false == m_source->Read(model, AssetBlob::Source, bytes))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the model file could not be read", model.relativePath.c_str());
            return nullptr;
        }
        struct SideReader
        {
            const AssetSystem* assets;
            String folder;
            static Bool Read(void* user, const char* relativePath, Array<std::byte>& out)
            {
                const SideReader& self = *static_cast<const SideReader*>(user);
                String path = self.folder;
                path += relativePath;
                return self.assets->ReadLooseFile(path, out);
            }
        };
        const std::size_t slash = model.relativePath.find_last_of('/');
        SideReader side{this, slash == String::npos ? String() : String(model.relativePath.substr(0, slash + 1))};
        ModelFileReader reader;
        reader.read = &SideReader::Read;
        reader.user = &side;
        ModelData decoded;
        String error;
        if (false == DecodeModel({bytes.Data(), static_cast<UInt32>(bytes.Size())}, ModelFormatFromPath(model.relativePath),
                meta.hasModelOptions ? meta.modelOptions : ModelImportOptions{}, reader, decoded, error))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: %s", model.relativePath.c_str(), error.c_str());
            return nullptr;
        }
        m_decodedModel = std::move(decoded);
        m_decodedModelId = model.id;
        return &m_decodedModel;
    }

    Bool AssetSystem::ReadMesh(const AssetRecord& record, MeshData& data)
    {
        // 빌드가 꺼내 둔 메시가 있으면 그것이다(6 단계) - 게임은 모델을 읽지 않는다. 패키지의 레코드는 이름이 없어 아이디로 찾는다.
        if (m_source != nullptr && m_source->Has(record, AssetBlob::CookedMesh))
        {
            Array<std::byte> cooked;
            if (false == m_source->Read(record, AssetBlob::CookedMesh, cooked)
                || false == ReadCookedMesh(cooked, data.vertices, data.indices, data.hasUvs, data.hasTangents, data.material))
            {
                Log::Write(LogLevel::Warning, "asset", "%s: the cooked mesh is damaged", record.relativePath.c_str());
                return false;
            }
            return true;
        }
        // 하위 메시는 모델 파일 안의 이름이 그 메시다(`AssetRecord::name`). 모델 레코드의 메타가 옵션을 든다.
        const AssetRecord* model = OwnerModelOf(record);
        const ModelData* decoded = model != nullptr ? DecodeOwnerModel(*model) : nullptr;
        if (decoded == nullptr)
        {
            return false;
        }
        Array<ModelSubAsset> listed;
        ListModelSubAssets(*decoded, listed);
        if (const ModelSubAsset* sub = FindModelSubAsset(listed, AssetType::Mesh, record.name))
        {
            const ModelMesh& mesh = decoded->meshes[sub->index.Get()];
            data.vertices = mesh.vertices;
            data.indices = mesh.indices;
            data.hasUvs = mesh.hasUvs;
            data.hasTangents = mesh.hasTangents;
            data.material = m_registry != nullptr ? FindModelMeshMaterialId(*m_registry, *model, listed, mesh) : AssetId{};
            // 스킨 메시(D-456). 뼈대는 같은 모델의 하위 Skeleton 이다.
            data.skin = mesh.skin;
            data.skeleton = mesh.skeleton >= 0 && m_registry != nullptr
                ? FindModelSubAssetId(*m_registry, *model, listed, AssetType::Skeleton, static_cast<std::size_t>(mesh.skeleton.Get())) : AssetId{};
            return true;
        }
        // 메타의 목록이 파일보다 낡았다(파일이 바뀌고 목록을 아직 맞추지 않음). 에디터의 파일 감시가 맞춘다.
        Log::Write(LogLevel::Warning, "asset", "%s: the model has no mesh named %s", model->relativePath.c_str(), record.name.c_str());
        return false;
    }

    Bool AssetSystem::ReadMaterial(const AssetRecord& record, MaterialData& data)
    {
        if (m_source == nullptr)
        {
            return false;
        }
        String error;
        // 갈래는 글자가 정한다 - 빌드가 옮긴 것과 모델의 재질은 늘 표준 머티리얼이다. 재로드가 같은 자리를 다시 쓰므로 둘 다 비우고 시작한다.
        data.standard = true;
        data.desc = MaterialDesc{};
        data.text.clear();
        // 빌드가 옮겨 둔 것이 먼저다 - 게임은 모델을 읽지 않는다. 그림 칸의 아이디까지 풀려 있다.
        if (m_source->Has(record, AssetBlob::CookedMaterial))
        {
            Array<std::byte> cooked;
            if (false == m_source->Read(record, AssetBlob::CookedMaterial, cooked)
                || false == ParseMaterialFile(reinterpret_cast<const char*>(cooked.Data()), cooked.Size(), data.desc, error))
            {
                Log::Write(LogLevel::Warning, "asset", "%s: the cooked material is damaged", record.relativePath.c_str());
                return false;
            }
            return true;
        }
        // 모델의 하위 Material 은 모델의 재질을 옮긴 것이다(읽기 전용, D-399 (2)).
        if (const AssetRecord* model = OwnerModelOf(record))
        {
            const ModelData* decoded = DecodeOwnerModel(*model);
            if (decoded == nullptr)
            {
                return false;
            }
            Array<ModelSubAsset> listed;
            ListModelSubAssets(*decoded, listed);
            const ModelSubAsset* sub = FindModelSubAsset(listed, AssetType::Material, record.name);
            if (sub == nullptr)
            {
                Log::Write(LogLevel::Warning, "asset", "%s: the model has no material named %s", model->relativePath.c_str(), record.name.c_str());
                return false;
            }
            UInt32 missing = 0;
            data.desc = MakeModelMaterialDesc(*m_registry, *model, *decoded, listed, sub->index.Get(), missing);
            if (missing > 0)
            {
                Log::Write(LogLevel::Warning, "asset", "%s: %u picture(s) of material %s are not in the project", model->relativePath.c_str(),
                    static_cast<unsigned>(missing.Get()), record.name.c_str());
            }
            return true;
        }
        Array<std::byte> text;
        if (false == m_source->Read(record, AssetBlob::Source, text))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the material file could not be read", record.relativePath.c_str());
            return false;
        }
        // 셰이더 머티리얼(D-347)은 글자째 든다 - 그래픽 모듈의 `MaterialLibrary` 가 셰이더를 보고 푼다.
        if (false == IsStandardMaterialText(reinterpret_cast<const char*>(text.Data()), text.Size()))
        {
            data.standard = false;
            data.text.assign(reinterpret_cast<const char*>(text.Data()), text.Size());
            return true;
        }
        if (false == ParseMaterialFile(reinterpret_cast<const char*>(text.Data()), text.Size(), data.desc, error))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: %s", record.relativePath.c_str(), error.c_str());
            return false;
        }
        return true;
    }

    const MaterialData* AssetSystem::GetMaterial(AssetHandle handle) const
    {
        const Slot<MaterialData>* slot = FindSlot(m_materials, handle, AssetType::Material);
        return slot != nullptr && false == slot->pending ? &slot->data : nullptr;
    }

    Bool AssetSystem::CopySharedImage(const AssetRecord& record, TextureData& data) const
    {
        if (record.sharedImageKey == 0)
        {
            return false;
        }
        for (const Slot<TextureData>& slot : m_textures.slots)
        {
            if (slot.occupied && false == slot.pending && slot.id != record.id && slot.data.sharedImageKey == record.sharedImageKey && slot.data.width > 0)
            {
                data.width = slot.data.width;
                data.height = slot.data.height;
                data.pixels = slot.data.pixels;
                return true;
            }
        }
        return false;
    }

    const AssetRecord* AssetSystem::CookedImageOf(const AssetRecord& record) const
    {
        if (m_source->Has(record, AssetBlob::CookedTexture))
        {
            return &record;
        }
        if (record.sharedImageKey == 0 || m_registry == nullptr)
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_registry->GetCount(); ++index)
        {
            const AssetRecord& other = m_registry->GetRecord(index);
            if (other.id != record.id && other.sharedImageKey == record.sharedImageKey && m_source->Has(other, AssetBlob::CookedTexture))
            {
                return &other;
            }
        }
        return nullptr;
    }

    Bool AssetSystem::ReadEmbeddedTexture(const AssetRecord& record, const AssetRecord& model, TextureData& data)
    {
        const ModelData* decoded = DecodeOwnerModel(model);
        if (decoded == nullptr)
        {
            return false;
        }
        Array<ModelSubAsset> listed;
        ListModelSubAssets(*decoded, listed);
        const ModelSubAsset* sub = FindModelSubAsset(listed, AssetType::Texture, record.name);
        if (sub == nullptr)
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the model has no embedded image named %s", model.relativePath.c_str(), record.name.c_str());
            return false;
        }
        const ModelImage& image = decoded->images[sub->index.Get()];
        JArrayView<std::byte> view;
        view.data = image.embedded.Data();
        view.size = static_cast<JBro::UInt32>(image.embedded.Size());
        DecodedImage pixels;
        if (false == DecodeImage(view, pixels))
        {
            Log::Write(LogLevel::Warning, "asset", "%s: the embedded image %s is not one this engine can decode",
                model.relativePath.c_str(), record.name.c_str());
            return false;
        }
        data.width = pixels.width;
        data.height = pixels.height;
        data.pixels = std::move(pixels.pixels);
        data.filter = data.options.filter == TextureFilter::Default ? m_defaultTextureFilter : data.options.filter;
        return true;
    }

    const StringTableData* AssetSystem::GetStringTable(AssetHandle handle) const
    {
        const Slot<StringTableData>* slot = FindSlot(m_stringTables, handle, AssetType::StringTable);
        return slot != nullptr ? &slot->data : nullptr;
    }

    const FontFamilyData* AssetSystem::GetFontFamily(AssetHandle handle) const
    {
        const Slot<FontFamilyData>* slot = FindSlot(m_fontFamilies, handle, AssetType::FontFamily);
        return slot != nullptr ? &slot->data : nullptr;
    }

    const FontData* AssetSystem::GetFont(AssetHandle handle) const
    {
        const Slot<FontData>* slot = FindSlot(m_fonts, handle, AssetType::Font);
        return slot != nullptr ? &slot->data : nullptr;
    }

    void AssetSystem::NotifyAudioRelease(UInt32 slotIndex)
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

    Bool AssetSystem::ComputeAudioPeaks(AssetHandle handle, UInt32 buckets, Array<Float>& peaks)
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
            bytes.size = static_cast<JBro::UInt32>(data->encoded.Size());
            return JBro::ComputeAudioPeaks(bytes, buckets, peaks);
        }
        if (false == data->streamPath.empty() && m_source != nullptr)
        {
            AudioFileDecoder decoder;
            return decoder.Open(m_source->OpenStream(data->streamPath.c_str()), data->streamPath.c_str())
                && JBro::ComputeAudioPeaks(decoder, buckets, peaks);
        }
        return false;
    }

    void AssetSystem::SetAudioReleaseListener(AudioReleaseCallback callback, void* user)
    {
        m_audioRelease = callback;
        m_audioReleaseUser = callback != nullptr ? user : nullptr;
    }

    void AssetSystem::SetAudioDecodeSampleRate(UInt32 sampleRate)
    {
        m_audioDecodeSampleRate = sampleRate;
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

    void AssetSystem::SetProjectFonts(ArrayView<const AssetId> fonts)
    {
        // 같은 목록이면 판번호를 올리지 않는다. 설정을 저장할 때마다 부르므로, 올리면 모든 텍스트가 다시 레이아웃된다.
        Bool same = fonts.Size() == m_projectFonts.Size();
        for (std::size_t index = 0; same && index < fonts.Size(); ++index)
        {
            same = fonts[index] == m_projectFonts[index];
        }
        if (same)
        {
            return;
        }
        m_projectFonts.Clear();
        for (std::size_t index = 0; index < fonts.Size(); ++index)
        {
            m_projectFonts.Add(fonts[index]);
        }
        ++m_projectFontsRevision;
    }

    ArrayView<const AssetId> AssetSystem::GetProjectFonts() const
    {
        return ArrayView<const AssetId>(m_projectFonts.Data(), m_projectFonts.Size());
    }

    UInt32 AssetSystem::GetProjectFontsRevision() const
    {
        return m_projectFontsRevision;
    }

    Bool AssetSystem::BuildSprite(const AssetRecord& record, SpriteData& data)
    {
        SpriteData built;
        if (false == ReadSpriteOptions(record, built.options))
        {
            return false;
        }
        // 텍스처를 먼저 잡는다. 스프라이트가 사는 동안 텍스처는 내려가지 않는다. 프레임을 지으려면 크기가 지금 있어야 한다 - 미룬 텍스처면 여기서 채운다(D-455).
        built.texture = Load(record.owner);
        FinishPendingTextureNow(built.texture);
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

    void AssetSystem::SetDeferredDecode(Bool deferred)
    {
        m_deferDecode = deferred;
    }

    Bool AssetSystem::IsDeferredDecode() const
    {
        return m_deferDecode;
    }

    Bool AssetSystem::IsPending(AssetHandle handle) const
    {
        switch (GetHandleType(handle))
        {
        case AssetType::Texture:
        {
            const Slot<TextureData>* slot = FindSlot(m_textures, handle, AssetType::Texture);
            return slot != nullptr && slot->pending;
        }
        case AssetType::Mesh:
        {
            const Slot<MeshData>* slot = FindSlot(m_meshes, handle, AssetType::Mesh);
            return slot != nullptr && slot->pending;
        }
        case AssetType::Material:
        {
            const Slot<MaterialData>* slot = FindSlot(m_materials, handle, AssetType::Material);
            return slot != nullptr && slot->pending;
        }
        case AssetType::EnvironmentMap:
        {
            const Slot<EnvironmentMapData>* slot = FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap);
            return slot != nullptr && slot->pending;
        }
        case AssetType::Skeleton:
        {
            const Slot<SkeletonData>* slot = FindSlot(m_skeletons, handle, AssetType::Skeleton);
            return slot != nullptr && slot->pending;
        }
        case AssetType::AnimationClip:
        {
            const Slot<AnimationClipData>* slot = FindSlot(m_animationClips, handle, AssetType::AnimationClip);
            return slot != nullptr && slot->pending;
        }
        default:
            return false;
        }
    }

    Bool AssetSystem::TryDeferLoad(const AssetRecord& record, AssetHandle& handle)
    {
        if (m_source == nullptr || false == m_source->CanReadOnWorkers())
        {
            return false;
        }
        const AssetRecord* model = OwnerModelOf(record);
        switch (record.type)
        {
        case AssetType::Texture:
        {
            // 빌드가 꺼내 둔 픽셀과 이미 실린 같은 그림은 복사뿐이다.
            if (CookedImageOf(record) != nullptr)
            {
                return false;
            }
            if (record.sharedImageKey != 0)
            {
                for (const Slot<TextureData>& slot : m_textures.slots)
                {
                    if (slot.occupied && false == slot.pending && slot.data.sharedImageKey == record.sharedImageKey && slot.data.width > 0)
                    {
                        return false;
                    }
                }
            }
            handle = Occupy(m_textures, AssetType::Texture, record.id, TextureData{});
            Slot<TextureData>* slot = FindSlot(m_textures, handle, AssetType::Texture);
            if (slot == nullptr)
            {
                return false;
            }
            slot->pending = true;
            slot->queued = false;
            return true;
        }
        case AssetType::Mesh:
        case AssetType::Material:
        {
            // 모델의 하위만 미룬다. `.jmat` 파일은 글자 하나를 읽을 뿐이고, 디코드해 둔 모델이면 옮길 뿐이다.
            if (model == nullptr || m_decodedModelId == model->id || m_source->Has(record, AssetBlob::CookedMesh)
                || m_source->Has(record, AssetBlob::CookedMaterial))
            {
                return false;
            }
            if (record.type == AssetType::Mesh)
            {
                handle = Occupy(m_meshes, AssetType::Mesh, record.id, MeshData{});
                Slot<MeshData>* slot = FindSlot(m_meshes, handle, AssetType::Mesh);
                if (slot == nullptr)
                {
                    return false;
                }
                slot->pending = true;
                slot->queued = false;
                return true;
            }
            handle = Occupy(m_materials, AssetType::Material, record.id, MaterialData{});
            Slot<MaterialData>* slot = FindSlot(m_materials, handle, AssetType::Material);
            if (slot == nullptr)
            {
                return false;
            }
            slot->pending = true;
            slot->queued = false;
            return true;
        }
        case AssetType::Skeleton:
        case AssetType::AnimationClip:
        {
            // 모델의 하위다(D-456). 디코드해 둔 모델이면 옮길 뿐이다.
            if (model == nullptr || m_decodedModelId == model->id)
            {
                return false;
            }
            if (record.type == AssetType::Skeleton)
            {
                handle = Occupy(m_skeletons, AssetType::Skeleton, record.id, SkeletonData{});
                Slot<SkeletonData>* slot = FindSlot(m_skeletons, handle, AssetType::Skeleton);
                if (slot == nullptr)
                {
                    return false;
                }
                slot->pending = true;
                slot->queued = false;
                return true;
            }
            handle = Occupy(m_animationClips, AssetType::AnimationClip, record.id, AnimationClipData{});
            Slot<AnimationClipData>* slot = FindSlot(m_animationClips, handle, AssetType::AnimationClip);
            if (slot == nullptr)
            {
                return false;
            }
            slot->pending = true;
            slot->queued = false;
            return true;
        }
        case AssetType::EnvironmentMap:
        {
            handle = Occupy(m_environmentMaps, AssetType::EnvironmentMap, record.id, EnvironmentMapData{});
            Slot<EnvironmentMapData>* slot = FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap);
            if (slot == nullptr)
            {
                return false;
            }
            slot->pending = true;
            slot->queued = false;
            return true;
        }
        default:
            return false;
        }
    }

    void AssetSystem::FinishPendingTextureNow(AssetHandle handle)
    {
        Slot<TextureData>* slot = FindSlot(m_textures, handle, AssetType::Texture);
        const AssetRecord* record = slot != nullptr && slot->pending && m_registry != nullptr ? m_registry->Find(slot->id) : nullptr;
        if (record == nullptr)
        {
            return;
        }
        TextureData fresh;
        const Bool read = ReadTexture(*record, fresh);
        // 다시 디코드하지 않게 잡은 자리를 다시 찾는다 - 내장 그림이면 모델을 읽는 사이 풀이 늘었을 수 있다.
        slot = FindSlot(m_textures, handle, AssetType::Texture);
        if (slot == nullptr)
        {
            return;
        }
        const UInt32 generation = slot->data.pixelGeneration + 1;
        slot->data = read ? std::move(fresh) : TextureData{};
        slot->data.pixelGeneration = generation;
        slot->pending = false;
        slot->queued = false;
        // 도는 작업이 있으면 그 결과는 버려진다.
        ++slot->requestSerial;
    }

    UInt32 AssetSystem::TakeDeferredJobs(Array<AssetDecodeJob>& jobs)
    {
        if (false == IsBound() || m_source == nullptr)
        {
            return 0;
        }
        UInt32 added = 0;
        // 모델마다 작업 하나다. 같은 모델의 자리를 그 작업에 모은다.
        Array<AssetId> modelIds;
        Array<std::size_t> modelJobs;
        const auto jobForModel = [&](const AssetRecord& model) -> AssetDecodeJob* {
            for (std::size_t at = 0; at < modelIds.Size(); ++at)
            {
                if (modelIds[at] == model.id)
                {
                    return &jobs[modelJobs[at]];
                }
            }
            AssetMetaFile meta;
            if (false == ReadMeta(model, meta))
            {
                return nullptr;
            }
            AssetDecodeJob job;
            job.id = model.id;
            job.type = AssetType::Model;
            job.record = model;
            job.bindSerial = m_bindSerial;
            job.modelOptions = meta.hasModelOptions ? meta.modelOptions : ModelImportOptions{};
            job.platform = m_platform;
            job.assetRoot = m_assetRoot;
            modelIds.Add(model.id);
            modelJobs.Add(jobs.Size());
            jobs.Add(std::move(job));
            ++added;
            return &jobs[jobs.Size() - 1];
        };
        const auto give = [&](auto& pool, AssetType type) {
            for (UInt32 index = 0; index < pool.slots.Size(); ++index)
            {
                auto& slot = pool.slots[index];
                if (false == slot.occupied || false == slot.pending || slot.queued)
                {
                    continue;
                }
                const AssetRecord* record = m_registry->Find(slot.id);
                if (record == nullptr)
                {
                    slot.pending = false;
                    continue;
                }
                AssetDecodeJob::Target target;
                target.handle = MakeHandle(type, index, slot.generation);
                target.serial = slot.requestSerial;
                if (const AssetRecord* model = OwnerModelOf(*record))
                {
                    AssetDecodeJob* job = jobForModel(*model);
                    if (job == nullptr)
                    {
                        // 모델의 메타가 읽히지 않는다. 빈 자료로 끝낸다(그리는 쪽은 빈 것을 건너뛴다).
                        slot.pending = false;
                        continue;
                    }
                    if (type == AssetType::Texture)
                    {
                        target.embeddedName = record->name;
                    }
                    job->targets.Add(std::move(target));
                    slot.queued = true;
                    continue;
                }
                AssetDecodeJob job;
                job.id = record->id;
                job.type = type;
                job.record = *record;
                job.bindSerial = m_bindSerial;
                if (type == AssetType::Texture && false == ReadTextureOptions(*record, job.texture.options))
                {
                    slot.pending = false;
                    continue;
                }
                job.targets.Add(std::move(target));
                jobs.Add(std::move(job));
                slot.queued = true;
                ++added;
            }
        };
        give(m_textures, AssetType::Texture);
        give(m_meshes, AssetType::Mesh);
        give(m_materials, AssetType::Material);
        give(m_environmentMaps, AssetType::EnvironmentMap);
        give(m_skeletons, AssetType::Skeleton);
        give(m_animationClips, AssetType::AnimationClip);
        return added;
    }

    template <typename TData>
    AssetSystem::Slot<TData>* AssetSystem::FindPendingTarget(Pool<TData>& pool, AssetType type, const AssetDecodeJob::Target& target)
    {
        Slot<TData>* slot = FindSlot(pool, target.handle, type);
        return slot != nullptr && slot->pending && slot->requestSerial == target.serial ? slot : nullptr;
    }

    void AssetSystem::AdoptDeferred(AssetDecodeJob& job)
    {
        if (false == IsBound() || job.bindSerial != m_bindSerial)
        {
            return;
        }
        if (false == job.decoded && false == job.failure.empty())
        {
            Log::Write(LogLevel::Warning, "asset", "%s", job.failure.c_str());
        }
        // 디코드한 모델은 들고 있는다 - 같은 모델의 다음 메시·재질은 옮길 뿐이다.
        if (job.decoded && job.type == AssetType::Model)
        {
            m_decodedModel = std::move(job.model);
            m_decodedModelId = job.record.id;
        }
        for (std::size_t at = 0; at < job.targets.Size(); ++at)
        {
            const AssetDecodeJob::Target& target = job.targets[at];
            switch (GetHandleType(target.handle))
            {
            case AssetType::Texture:
            {
                Slot<TextureData>* slot = FindPendingTarget(m_textures, AssetType::Texture, target);
                const AssetRecord* record = slot != nullptr ? m_registry->Find(slot->id) : nullptr;
                if (slot == nullptr)
                {
                    break;
                }
                TextureData fresh;
                if (job.decoded && record != nullptr)
                {
                    if (job.type == AssetType::Model)
                    {
                        if (at < job.embedded.Size() && job.embedded[at].width > 0 && ReadTextureOptions(*record, fresh.options))
                        {
                            fresh.width = job.embedded[at].width;
                            fresh.height = job.embedded[at].height;
                            fresh.pixels = std::move(job.embedded[at].pixels);
                        }
                        else
                        {
                            Log::Write(LogLevel::Warning, "asset", "%s: the embedded image %s could not be decoded", job.record.relativePath.c_str(),
                                record->name.c_str());
                        }
                    }
                    else
                    {
                        fresh.options = job.texture.options;
                        fresh.width = job.texture.width;
                        fresh.height = job.texture.height;
                        fresh.pixels = std::move(job.texture.pixels);
                    }
                    fresh.sharedImageKey = record->sharedImageKey;
                    fresh.filter = fresh.options.filter == TextureFilter::Default ? m_defaultTextureFilter : fresh.options.filter;
                }
                fresh.pixelGeneration = slot->data.pixelGeneration + 1;
                slot->data = std::move(fresh);
                slot->pending = false;
                slot->queued = false;
                break;
            }
            case AssetType::EnvironmentMap:
            {
                Slot<EnvironmentMapData>* slot = FindPendingTarget(m_environmentMaps, AssetType::EnvironmentMap, target);
                if (slot == nullptr)
                {
                    break;
                }
                EnvironmentMapData fresh = job.decoded ? std::move(job.environment) : EnvironmentMapData{};
                fresh.dataGeneration = slot->data.dataGeneration + 1;
                slot->data = std::move(fresh);
                slot->pending = false;
                slot->queued = false;
                break;
            }
            case AssetType::Mesh:
            {
                Slot<MeshData>* slot = FindPendingTarget(m_meshes, AssetType::Mesh, target);
                const AssetRecord* record = slot != nullptr ? m_registry->Find(slot->id) : nullptr;
                if (slot == nullptr)
                {
                    break;
                }
                // 들고 있는 모델에서 옮긴다(`ReadMesh` 가 디코드한 모델을 본다). 모델이 풀리지 않았으면 빈 메시다.
                MeshData fresh;
                if (false == job.decoded || record == nullptr || m_decodedModelId != job.record.id || false == ReadMesh(*record, fresh))
                {
                    fresh = MeshData{};
                }
                slot = FindSlot(m_meshes, target.handle, AssetType::Mesh);
                if (slot == nullptr)
                {
                    break;
                }
                fresh.dataGeneration = slot->data.dataGeneration + 1;
                slot->data = std::move(fresh);
                slot->pending = false;
                slot->queued = false;
                break;
            }
            case AssetType::Material:
            {
                Slot<MaterialData>* slot = FindPendingTarget(m_materials, AssetType::Material, target);
                const AssetRecord* record = slot != nullptr ? m_registry->Find(slot->id) : nullptr;
                if (slot == nullptr)
                {
                    break;
                }
                MaterialData fresh;
                if (false == job.decoded || record == nullptr || m_decodedModelId != job.record.id || false == ReadMaterial(*record, fresh))
                {
                    fresh = MaterialData{};
                }
                slot = FindSlot(m_materials, target.handle, AssetType::Material);
                if (slot == nullptr)
                {
                    break;
                }
                fresh.dataGeneration = slot->data.dataGeneration + 1;
                slot->data = std::move(fresh);
                slot->pending = false;
                slot->queued = false;
                break;
            }
            case AssetType::Skeleton:
            {
                Slot<SkeletonData>* slot = FindPendingTarget(m_skeletons, AssetType::Skeleton, target);
                const AssetRecord* record = slot != nullptr ? m_registry->Find(slot->id) : nullptr;
                if (slot == nullptr)
                {
                    break;
                }
                SkeletonData fresh;
                if (false == job.decoded || record == nullptr || m_decodedModelId != job.record.id || false == ReadSkeleton(*record, fresh))
                {
                    fresh = SkeletonData{};
                }
                slot = FindSlot(m_skeletons, target.handle, AssetType::Skeleton);
                if (slot == nullptr)
                {
                    break;
                }
                fresh.dataGeneration = slot->data.dataGeneration + 1;
                slot->data = std::move(fresh);
                slot->pending = false;
                slot->queued = false;
                break;
            }
            case AssetType::AnimationClip:
            {
                Slot<AnimationClipData>* slot = FindPendingTarget(m_animationClips, AssetType::AnimationClip, target);
                const AssetRecord* record = slot != nullptr ? m_registry->Find(slot->id) : nullptr;
                if (slot == nullptr)
                {
                    break;
                }
                AnimationClipData fresh;
                if (false == job.decoded || record == nullptr || m_decodedModelId != job.record.id || false == ReadAnimationClip(*record, fresh))
                {
                    fresh = AnimationClipData{};
                }
                slot = FindSlot(m_animationClips, target.handle, AssetType::AnimationClip);
                if (slot == nullptr)
                {
                    break;
                }
                fresh.dataGeneration = slot->data.dataGeneration + 1;
                slot->data = std::move(fresh);
                slot->pending = false;
                slot->queued = false;
                break;
            }
            default:
                break;
            }
        }
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
            case AssetType::Font:
                if (Slot<FontData>* slot = FindSlot(m_fonts, handle, AssetType::Font))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::FontFamily:
                if (Slot<FontFamilyData>* slot = FindSlot(m_fontFamilies, handle, AssetType::FontFamily))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::StringTable:
                if (Slot<StringTableData>* slot = FindSlot(m_stringTables, handle, AssetType::StringTable))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::AudioEffect:
                if (Slot<AudioEffectData>* slot = FindSlot(m_audioEffects, handle, AssetType::AudioEffect))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::ColorLut:
                if (Slot<ColorLutData>* slot = FindSlot(m_colorLuts, handle, AssetType::ColorLut))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::EnvironmentMap:
                if (Slot<EnvironmentMapData>* slot = FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::PostProcessProfile:
                if (Slot<PostProcessProfileData>* slot = FindSlot(m_postProcessProfiles, handle, AssetType::PostProcessProfile))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::RenderTexture:
                if (Slot<RenderTextureData>* slot = FindSlot(m_renderTextures, handle, AssetType::RenderTexture))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::Shader:
                if (Slot<ShaderAssetData>* slot = FindSlot(m_shaders, handle, AssetType::Shader))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::Material:
                if (Slot<MaterialData>* slot = FindSlot(m_materials, handle, AssetType::Material))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::Mesh:
                if (Slot<MeshData>* slot = FindSlot(m_meshes, handle, AssetType::Mesh))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::Skeleton:
                if (Slot<SkeletonData>* slot = FindSlot(m_skeletons, handle, AssetType::Skeleton))
                {
                    ++slot->referenceCount;
                    return handle;
                }
                break;
            case AssetType::AnimationClip:
                if (Slot<AnimationClipData>* slot = FindSlot(m_animationClips, handle, AssetType::AnimationClip))
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
        if (m_deferDecode && TryDeferLoad(*record, handle))
        {
            m_loaded.TryAdd(id, handle);
            return handle;
        }
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
        case AssetType::Font:
        {
            FontData data;
            if (false == ReadFont(*record, data))
            {
                return {};
            }
            handle = Occupy(m_fonts, AssetType::Font, id, std::move(data));
            break;
        }
        case AssetType::FontFamily:
        {
            FontFamilyData data;
            if (false == ReadFontFamily(*record, data))
            {
                return {};
            }
            FontFamilyData kept = data;
            handle = Occupy(m_fontFamilies, AssetType::FontFamily, id, std::move(data));
            if (handle.generation == 0)
            {
                // 풀이 찼다. 칸이 잡은 폰트를 놓아야 폰트가 샌 채로 남지 않는다.
                ReleaseFamilyFonts(kept);
            }
            break;
        }
        case AssetType::StringTable:
        {
            StringTableData data;
            if (false == ReadStringTable(*record, data))
            {
                return {};
            }
            handle = Occupy(m_stringTables, AssetType::StringTable, id, std::move(data));
            break;
        }
        case AssetType::AudioEffect:
        {
            AudioEffectData data;
            if (false == ReadAudioEffect(*record, data))
            {
                return {};
            }
            handle = Occupy(m_audioEffects, AssetType::AudioEffect, id, std::move(data));
            break;
        }
        case AssetType::ColorLut:
        {
            ColorLutData data;
            if (false == ReadColorLut(*record, data))
            {
                return {};
            }
            handle = Occupy(m_colorLuts, AssetType::ColorLut, id, std::move(data));
            break;
        }
        case AssetType::EnvironmentMap:
        {
            EnvironmentMapData data;
            if (false == ReadEnvironmentMap(*record, data))
            {
                return {};
            }
            handle = Occupy(m_environmentMaps, AssetType::EnvironmentMap, id, std::move(data));
            break;
        }
        case AssetType::PostProcessProfile:
        {
            PostProcessProfileData data;
            if (false == ReadPostProcessProfile(*record, data))
            {
                return {};
            }
            handle = Occupy(m_postProcessProfiles, AssetType::PostProcessProfile, id, std::move(data));
            break;
        }
        case AssetType::RenderTexture:
        {
            RenderTextureData data;
            if (false == ReadRenderTexture(*record, data))
            {
                return {};
            }
            handle = Occupy(m_renderTextures, AssetType::RenderTexture, id, std::move(data));
            break;
        }
        case AssetType::Shader:
        {
            ShaderAssetData data;
            if (false == ReadShader(*record, data))
            {
                return {};
            }
            handle = Occupy(m_shaders, AssetType::Shader, id, std::move(data));
            break;
        }
        case AssetType::Material:
        {
            MaterialData data;
            if (false == ReadMaterial(*record, data))
            {
                return {};
            }
            handle = Occupy(m_materials, AssetType::Material, id, std::move(data));
            break;
        }
        case AssetType::Mesh:
        {
            MeshData data;
            if (false == ReadMesh(*record, data))
            {
                return {};
            }
            handle = Occupy(m_meshes, AssetType::Mesh, id, std::move(data));
            break;
        }
        case AssetType::Skeleton:
        {
            SkeletonData data;
            if (false == ReadSkeleton(*record, data))
            {
                return {};
            }
            handle = Occupy(m_skeletons, AssetType::Skeleton, id, std::move(data));
            break;
        }
        case AssetType::AnimationClip:
        {
            AnimationClipData data;
            if (false == ReadAnimationClip(*record, data))
            {
                return {};
            }
            handle = Occupy(m_animationClips, AssetType::AnimationClip, id, std::move(data));
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
            return;
        }
        if (Slot<FontData>* font = FindSlot(m_fonts, handle, AssetType::Font))
        {
            if (font->referenceCount != 0)
            {
                --font->referenceCount;
            }
            return;
        }
        if (Slot<FontFamilyData>* family = FindSlot(m_fontFamilies, handle, AssetType::FontFamily))
        {
            if (family->referenceCount != 0)
            {
                --family->referenceCount;
            }
            return;
        }
        if (Slot<StringTableData>* table = FindSlot(m_stringTables, handle, AssetType::StringTable))
        {
            if (table->referenceCount != 0)
            {
                --table->referenceCount;
            }
            return;
        }
        if (Slot<AudioEffectData>* effect = FindSlot(m_audioEffects, handle, AssetType::AudioEffect))
        {
            if (effect->referenceCount != 0)
            {
                --effect->referenceCount;
            }
            return;
        }
        if (Slot<ColorLutData>* lut = FindSlot(m_colorLuts, handle, AssetType::ColorLut))
        {
            if (lut->referenceCount != 0)
            {
                --lut->referenceCount;
            }
            return;
        }
        if (Slot<EnvironmentMapData>* sky = FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap))
        {
            if (sky->referenceCount != 0)
            {
                --sky->referenceCount;
            }
            return;
        }
        if (Slot<PostProcessProfileData>* profile = FindSlot(m_postProcessProfiles, handle, AssetType::PostProcessProfile))
        {
            if (profile->referenceCount != 0)
            {
                --profile->referenceCount;
            }
            return;
        }
        if (Slot<RenderTextureData>* target = FindSlot(m_renderTextures, handle, AssetType::RenderTexture))
        {
            if (target->referenceCount != 0)
            {
                --target->referenceCount;
            }
            return;
        }
        if (Slot<ShaderAssetData>* shader = FindSlot(m_shaders, handle, AssetType::Shader))
        {
            if (shader->referenceCount != 0)
            {
                --shader->referenceCount;
            }
            return;
        }
        if (Slot<MaterialData>* material = FindSlot(m_materials, handle, AssetType::Material))
        {
            if (material->referenceCount != 0)
            {
                --material->referenceCount;
            }
        }
        if (Slot<MeshData>* mesh = FindSlot(m_meshes, handle, AssetType::Mesh))
        {
            if (mesh->referenceCount != 0)
            {
                --mesh->referenceCount;
            }
        }
        if (Slot<SkeletonData>* skeleton = FindSlot(m_skeletons, handle, AssetType::Skeleton))
        {
            if (skeleton->referenceCount != 0)
            {
                --skeleton->referenceCount;
            }
        }
        if (Slot<AnimationClipData>* clip = FindSlot(m_animationClips, handle, AssetType::AnimationClip))
        {
            if (clip->referenceCount != 0)
            {
                --clip->referenceCount;
            }
        }
    }

    AssetHandle AssetSystem::Find(AssetId id) const
    {
        const AssetHandle* loaded = m_loaded.Find(id);
        return loaded != nullptr ? *loaded : AssetHandle{};
    }

    Bool AssetSystem::IsLoaded(AssetHandle handle) const
    {
        return FindSlot(m_textures, handle, AssetType::Texture) != nullptr
            || FindSlot(m_sprites, handle, AssetType::Sprite) != nullptr
            || FindSlot(m_audio, handle, AssetType::Audio) != nullptr
            || FindSlot(m_fonts, handle, AssetType::Font) != nullptr
            || FindSlot(m_fontFamilies, handle, AssetType::FontFamily) != nullptr
            || FindSlot(m_stringTables, handle, AssetType::StringTable) != nullptr
            || FindSlot(m_audioEffects, handle, AssetType::AudioEffect) != nullptr
            || FindSlot(m_colorLuts, handle, AssetType::ColorLut) != nullptr
            || FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap) != nullptr
            || FindSlot(m_postProcessProfiles, handle, AssetType::PostProcessProfile) != nullptr
            || FindSlot(m_renderTextures, handle, AssetType::RenderTexture) != nullptr
            || FindSlot(m_shaders, handle, AssetType::Shader) != nullptr
            || FindSlot(m_materials, handle, AssetType::Material) != nullptr
            || FindSlot(m_meshes, handle, AssetType::Mesh) != nullptr
            || FindSlot(m_skeletons, handle, AssetType::Skeleton) != nullptr
            || FindSlot(m_animationClips, handle, AssetType::AnimationClip) != nullptr;
    }

    UInt32 AssetSystem::GetReferenceCount(AssetHandle handle) const
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
        if (const Slot<FontData>* font = FindSlot(m_fonts, handle, AssetType::Font))
        {
            return font->referenceCount;
        }
        if (const Slot<FontFamilyData>* family = FindSlot(m_fontFamilies, handle, AssetType::FontFamily))
        {
            return family->referenceCount;
        }
        if (const Slot<StringTableData>* table = FindSlot(m_stringTables, handle, AssetType::StringTable))
        {
            return table->referenceCount;
        }
        if (const Slot<AudioEffectData>* effect = FindSlot(m_audioEffects, handle, AssetType::AudioEffect))
        {
            return effect->referenceCount;
        }
        if (const Slot<ColorLutData>* lut = FindSlot(m_colorLuts, handle, AssetType::ColorLut))
        {
            return lut->referenceCount;
        }
        if (const Slot<EnvironmentMapData>* sky = FindSlot(m_environmentMaps, handle, AssetType::EnvironmentMap))
        {
            return sky->referenceCount;
        }
        if (const Slot<PostProcessProfileData>* profile = FindSlot(m_postProcessProfiles, handle, AssetType::PostProcessProfile))
        {
            return profile->referenceCount;
        }
        if (const Slot<RenderTextureData>* target = FindSlot(m_renderTextures, handle, AssetType::RenderTexture))
        {
            return target->referenceCount;
        }
        if (const Slot<ShaderAssetData>* shader = FindSlot(m_shaders, handle, AssetType::Shader))
        {
            return shader->referenceCount;
        }
        if (const Slot<MaterialData>* material = FindSlot(m_materials, handle, AssetType::Material))
        {
            return material->referenceCount;
        }
        if (const Slot<MeshData>* mesh = FindSlot(m_meshes, handle, AssetType::Mesh))
        {
            return mesh->referenceCount;
        }
        if (const Slot<SkeletonData>* skeleton = FindSlot(m_skeletons, handle, AssetType::Skeleton))
        {
            return skeleton->referenceCount;
        }
        if (const Slot<AnimationClipData>* clip = FindSlot(m_animationClips, handle, AssetType::AnimationClip))
        {
            return clip->referenceCount;
        }
        return 0;
    }

    const TextureData* AssetSystem::GetTexture(AssetHandle handle) const
    {
        const Slot<TextureData>* slot = FindSlot(m_textures, handle, AssetType::Texture);
        return slot != nullptr && false == slot->pending ? &slot->data : nullptr;
    }

    const SpriteData* AssetSystem::GetSprite(AssetHandle handle) const
    {
        const Slot<SpriteData>* slot = FindSlot(m_sprites, handle, AssetType::Sprite);
        return slot != nullptr ? &slot->data : nullptr;
    }

    Bool AssetSystem::ReloadInPlace(AssetId id)
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
        // 아직 디코드 중이면 다시 내준다. 도는 작업의 결과는 번호가 달라 버려진다(D-455).
        if (IsPending(*loaded))
        {
            if (OwnerModelOf(*record) != nullptr)
            {
                m_decodedModelId = {};
                m_decodedModel = {};
            }
            const auto requeue = [](auto* slot) {
                if (slot != nullptr)
                {
                    ++slot->requestSerial;
                    slot->queued = false;
                }
            };
            requeue(FindSlot(m_textures, *loaded, AssetType::Texture));
            requeue(FindSlot(m_meshes, *loaded, AssetType::Mesh));
            requeue(FindSlot(m_materials, *loaded, AssetType::Material));
            requeue(FindSlot(m_environmentMaps, *loaded, AssetType::EnvironmentMap));
            requeue(FindSlot(m_skeletons, *loaded, AssetType::Skeleton));
            requeue(FindSlot(m_animationClips, *loaded, AssetType::AnimationClip));
            return true;
        }
        if (Slot<TextureData>* texture = FindSlot(m_textures, *loaded, AssetType::Texture))
        {
            // 내장 그림이면 모델 파일이 바뀌었을 수 있다 - 메시와 같이 들고 있던 모델을 버린다.
            if (OwnerModelOf(*record) != nullptr)
            {
                m_decodedModelId = {};
                m_decodedModel = {};
            }
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
        if (Slot<FontData>* font = FindSlot(m_fonts, *loaded, AssetType::Font))
        {
            FontData fresh;
            if (false == ReadFont(*record, fresh))
            {
                return false;
            }
            // 옛 바이트는 여기서 풀린다. 텍스트 시스템은 옛 바이트를 가리키는 face 를 들지 않는다 - 자기 사본을 연다(FontFace::Load).
            fresh.dataGeneration = font->data.dataGeneration + 1;
            font->data = std::move(fresh);
            return true;
        }
        if (Slot<FontFamilyData>* family = FindSlot(m_fontFamilies, *loaded, AssetType::FontFamily))
        {
            // 새 칸을 먼저 싣고 옛 칸을 놓는다 - 같은 폰트가 두 칸에 걸쳐 있으면 참조 수가 0 을 거치지 않는다.
            FontFamilyData fresh;
            if (false == ReadFontFamily(*record, fresh))
            {
                return false;
            }
            ReleaseFamilyFonts(family->data);
            fresh.dataGeneration = family->data.dataGeneration + 1;
            family->data = std::move(fresh);
            return true;
        }
        if (Slot<StringTableData>* table = FindSlot(m_stringTables, *loaded, AssetType::StringTable))
        {
            StringTableData fresh;
            if (false == ReadStringTable(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = table->data.dataGeneration + 1;
            table->data = std::move(fresh);
            return true;
        }
        if (Slot<AudioEffectData>* effect = FindSlot(m_audioEffects, *loaded, AssetType::AudioEffect))
        {
            AudioEffectData fresh;
            if (false == ReadAudioEffect(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = effect->data.dataGeneration + 1;
            effect->data = fresh;
            return true;
        }
        if (Slot<ColorLutData>* lut = FindSlot(m_colorLuts, *loaded, AssetType::ColorLut))
        {
            ColorLutData fresh;
            if (false == ReadColorLut(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = lut->data.dataGeneration + 1;
            lut->data = std::move(fresh);
            return true;
        }
        if (Slot<EnvironmentMapData>* sky = FindSlot(m_environmentMaps, *loaded, AssetType::EnvironmentMap))
        {
            EnvironmentMapData fresh;
            if (false == ReadEnvironmentMap(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = sky->data.dataGeneration + 1;
            sky->data = std::move(fresh);
            return true;
        }
        if (Slot<PostProcessProfileData>* profile = FindSlot(m_postProcessProfiles, *loaded, AssetType::PostProcessProfile))
        {
            PostProcessProfileData fresh;
            if (false == ReadPostProcessProfile(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = profile->data.dataGeneration + 1;
            profile->data = std::move(fresh);
            return true;
        }
        if (Slot<RenderTextureData>* target = FindSlot(m_renderTextures, *loaded, AssetType::RenderTexture))
        {
            RenderTextureData fresh;
            if (false == ReadRenderTexture(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = target->data.dataGeneration + 1;
            target->data = std::move(fresh);
            return true;
        }
        if (Slot<ShaderAssetData>* shader = FindSlot(m_shaders, *loaded, AssetType::Shader))
        {
            ShaderAssetData fresh;
            if (false == ReadShader(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = shader->data.dataGeneration + 1;
            shader->data = std::move(fresh);
            return true;
        }
        if (Slot<MaterialData>* material = FindSlot(m_materials, *loaded, AssetType::Material))
        {
            // 모델의 재질이면 모델 파일이 바뀌었을 수 있다 - 들고 있던 모델을 버린다.
            if (OwnerModelOf(*record) != nullptr)
            {
                m_decodedModelId = {};
                m_decodedModel = {};
            }
            MaterialData fresh;
            if (false == ReadMaterial(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = material->data.dataGeneration + 1;
            material->data = std::move(fresh);
            return true;
        }
        if (Slot<MeshData>* mesh = FindSlot(m_meshes, *loaded, AssetType::Mesh))
        {
            // 모델 파일이 바뀌었다는 뜻이다 - 들고 있던 모델을 버리고 다시 읽는다.
            m_decodedModelId = {};
            m_decodedModel = {};
            MeshData fresh;
            if (false == ReadMesh(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = mesh->data.dataGeneration + 1;
            mesh->data = std::move(fresh);
            return true;
        }
        if (Slot<SkeletonData>* skeleton = FindSlot(m_skeletons, *loaded, AssetType::Skeleton))
        {
            m_decodedModelId = {};
            m_decodedModel = {};
            SkeletonData fresh;
            if (false == ReadSkeleton(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = skeleton->data.dataGeneration + 1;
            skeleton->data = std::move(fresh);
            return true;
        }
        if (Slot<AnimationClipData>* clip = FindSlot(m_animationClips, *loaded, AssetType::AnimationClip))
        {
            m_decodedModelId = {};
            m_decodedModel = {};
            AnimationClipData fresh;
            if (false == ReadAnimationClip(*record, fresh))
            {
                return false;
            }
            fresh.dataGeneration = clip->data.dataGeneration + 1;
            clip->data = std::move(fresh);
            return true;
        }
        return false;
    }

    UInt32 AssetSystem::ReloadAllInPlace()
    {
        // 먼저 아이디를 모은다. 재로드는 표를 바꾸지 않지만, 도는 동안 표를 만지지 않는 쪽이 안전하다.
        Array<AssetId> ids;
        ids.Reserve(m_loaded.Size());
        for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it)
        {
            ids.Add(it->KeyValue);
        }
        UInt32 reloaded = 0;
        for (std::size_t index = 0; index < ids.Size(); ++index)
        {
            if (ReloadInPlace(ids[index]))
            {
                ++reloaded;
            }
        }
        return reloaded;
    }

    UInt32 AssetSystem::CollectUnused()
    {
        UInt32 freed = 0;
        // 패밀리가 맨 먼저다. 그것이 놓는 폰트가 뒤의 순회에서 0 이 될 수 있다.
        for (UInt32 index = 0; index < m_fontFamilies.slots.Size(); ++index)
        {
            Slot<FontFamilyData>& slot = m_fontFamilies.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                ReleaseFamilyFonts(slot.data);
                Vacate(m_fontFamilies, index);
                ++freed;
            }
        }
        // 스프라이트가 먼저다. 그것이 놓는 텍스처가 두 번째 순회에서 0 이 될 수 있다.
        for (UInt32 index = 0; index < m_sprites.slots.Size(); ++index)
        {
            Slot<SpriteData>& slot = m_sprites.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Release(slot.data.texture);
                Vacate(m_sprites, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_textures.slots.Size(); ++index)
        {
            Slot<TextureData>& slot = m_textures.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_textures, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_audio.slots.Size(); ++index)
        {
            Slot<AudioData>& slot = m_audio.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                NotifyAudioRelease(index);
                Vacate(m_audio, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_fonts.slots.Size(); ++index)
        {
            Slot<FontData>& slot = m_fonts.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_fonts, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_stringTables.slots.Size(); ++index)
        {
            Slot<StringTableData>& slot = m_stringTables.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_stringTables, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_audioEffects.slots.Size(); ++index)
        {
            Slot<AudioEffectData>& slot = m_audioEffects.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_audioEffects, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_colorLuts.slots.Size(); ++index)
        {
            Slot<ColorLutData>& slot = m_colorLuts.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_colorLuts, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_environmentMaps.slots.Size(); ++index)
        {
            Slot<EnvironmentMapData>& slot = m_environmentMaps.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_environmentMaps, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_postProcessProfiles.slots.Size(); ++index)
        {
            Slot<PostProcessProfileData>& slot = m_postProcessProfiles.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_postProcessProfiles, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_renderTextures.slots.Size(); ++index)
        {
            Slot<RenderTextureData>& slot = m_renderTextures.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_renderTextures, index);
                ++freed;
            }
        }
        // 머티리얼이 먼저 - 셰이더는 머티리얼 라이브러리가 쥐므로 여기 순서와는 상관없지만 읽는 차례를 맞춘다.
        for (UInt32 index = 0; index < m_materials.slots.Size(); ++index)
        {
            Slot<MaterialData>& slot = m_materials.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_materials, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_shaders.slots.Size(); ++index)
        {
            Slot<ShaderAssetData>& slot = m_shaders.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_shaders, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_meshes.slots.Size(); ++index)
        {
            Slot<MeshData>& slot = m_meshes.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_meshes, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_skeletons.slots.Size(); ++index)
        {
            Slot<SkeletonData>& slot = m_skeletons.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_skeletons, index);
                ++freed;
            }
        }
        for (UInt32 index = 0; index < m_animationClips.slots.Size(); ++index)
        {
            Slot<AnimationClipData>& slot = m_animationClips.slots[index];
            if (slot.occupied && slot.referenceCount == 0)
            {
                Vacate(m_animationClips, index);
                ++freed;
            }
        }
        // 들고 있던 모델도 놓는다 - 다음 메시 로드가 다시 읽는다.
        m_decodedModelId = {};
        m_decodedModel = {};
        return freed;
    }

    UInt32 AssetSystem::GetLoadedCount() const
    {
        return static_cast<JBro::UInt32>(m_loaded.Size());
    }

    UInt32 AssetSystem::BindComponentAssets(const PropertyTable& table, void* component, Array<AssetHandle>& acquired)
    {
        if (component == nullptr)
        {
            return 0;
        }
        UInt32 bound = 0;
        ForEachAssetField(table, [&](const PropertyInfo& idProperty, const PropertyInfo& handleProperty) {
            const AssetId id = *static_cast<const AssetId*>(idProperty.ConstAddress(component));
            AssetHandle& target = *static_cast<AssetHandle*>(handleProperty.Address(component));
            target = id.IsNull() ? AssetHandle{} : Load(id);
            if (target.generation != 0)
            {
                acquired.Add(target);
                ++bound;
            }
        });
        // 목록 짝은 아이디 수만큼 핸들 칸을 맞춘 뒤 칸마다 싣는다. 칸 수를 바꾸는 것은 이 패스(캔버스 로드·편집 뒤)뿐이다 - 매 프레임이 아니다.
        ForEachAssetListField(table, [&](const PropertyInfo& idProperty, const PropertyInfo& handleProperty) {
            const ArrayOps& idOps = *idProperty.type->arrayOps;
            const ArrayOps& handleOps = *handleProperty.type->arrayOps;
            const void* ids = idProperty.ConstAddress(component);
            void* handles = handleProperty.Address(component);
            const std::size_t count = idOps.GetSize(ids);
            handleOps.Clear(handles);
            for (std::size_t index = 0; index < count; ++index)
            {
                if (false == handleOps.AddDefault(handles))
                {
                    break;
                }
                const AssetId id = *static_cast<const AssetId*>(idOps.GetConstElement(ids, index));
                AssetHandle& target = *static_cast<AssetHandle*>(handleOps.GetElement(handles, index));
                target = id.IsNull() ? AssetHandle{} : Load(id);
                if (target.generation != 0)
                {
                    acquired.Add(target);
                    ++bound;
                }
            }
        });
        return bound;
    }

    void AssetSystem::CollectComponentAssetIds(const PropertyTable& table, const void* component, Array<AssetId>& ids)
    {
        if (component == nullptr)
        {
            return;
        }
        ForEachAssetField(table, [&](const PropertyInfo& idProperty, const PropertyInfo&) {
            const AssetId id = *static_cast<const AssetId*>(idProperty.ConstAddress(component));
            if (false == id.IsNull() && false == ids.Contains(id))
            {
                ids.Add(id);
            }
        });
        ForEachAssetListField(table, [&](const PropertyInfo& idProperty, const PropertyInfo&) {
            const ArrayOps& idOps = *idProperty.type->arrayOps;
            const void* list = idProperty.ConstAddress(component);
            for (std::size_t index = 0; index < idOps.GetSize(list); ++index)
            {
                const AssetId id = *static_cast<const AssetId*>(idOps.GetConstElement(list, index));
                if (false == id.IsNull() && false == ids.Contains(id))
                {
                    ids.Add(id);
                }
            }
        });
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
