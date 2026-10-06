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
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro
{
    namespace
    {
        constexpr AssetHandle MakeHandle(AssetType type, UInt32 slotIndex, UInt32 generation) noexcept
        {
            AssetHandle handle;
            handle.index = (static_cast<JBro::UInt32>(type) << 28) | slotIndex;
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

    Bool AssetSystem::ReadTexture(const AssetRecord& record, TextureData& data)
    {
        // 메타를 먼저 본다. 읽히지 않는 메타면 디코드는 헛일이다.
        if (false == ReadTextureOptions(record, data.options))
        {
            return false;
        }
        // 빌드가 디코드해 둔 픽셀이 있으면 그것이다(D-232) - 게임에 이미지 디코더가 돌지 않는다.
        if (m_source->Has(record, AssetBlob::CookedTexture))
        {
            Array<std::byte> cooked;
            CookedTextureInfo info;
            if (false == m_source->Read(record, AssetBlob::CookedTexture, cooked) || false == ReadCookedTexture(cooked, info))
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
            || FindSlot(m_stringTables, handle, AssetType::StringTable) != nullptr;
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
