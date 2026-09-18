#include <JBro/Asset/Asset.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/Asset/SpriteFrames.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Types/NameTable.h>

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
        m_textures = {};
        m_sprites = {};
        m_loaded.Clear();
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

    String AssetSystem::MetaPathOf(const AssetRecord& record) const
    {
        return AssetTypeRules::MakeMetaPath(SourcePathOf(record));
    }

    bool AssetSystem::ReadTexture(const AssetRecord& record, TextureData& data)
    {
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
        return true;
    }

    bool AssetSystem::ReadSpriteOptions(const AssetRecord& record, SpriteImportOptions& options)
    {
        // 메타에 `Sprite.ImportOptions` 가 없으면 기본값이다. 있으면 전부 읽혀야 한다 - 읽히지 않는 값은 실패다.
        Array<std::byte> text;
        if (false == m_platform->ReadWholeFile(MetaPathOf(record).c_str(), text))
        {
            return false;
        }
        YamlDocument document;
        YamlError yamlError;
        if (false == document.Parse(reinterpret_cast<const char*>(text.Data()), text.Size(), yamlError))
        {
            return false;
        }
        const std::uint32_t sprite = document.Find(document.GetRoot(), "Sprite");
        if (sprite == YamlDocument::InvalidNode)
        {
            options = SpriteImportOptions{};
            return true;
        }
        const std::uint32_t block = document.Find(sprite, "ImportOptions");
        if (block == YamlDocument::InvalidNode)
        {
            options = SpriteImportOptions{};
            return true;
        }
        SpriteImportOptions read;
        ReflectedYamlError error;
        if (false == ReadReflectedValue(document, block, TypeDescriptorOf<SpriteImportOptions>::Get(), &read, error))
        {
            return false;
        }
        options = read;
        return true;
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
                ++FindSlot(m_textures, handle, AssetType::Texture)->referenceCount;
                break;
            case AssetType::Sprite:
                ++FindSlot(m_sprites, handle, AssetType::Sprite)->referenceCount;
                break;
            default:
                return {};
            }
            return handle;
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
            handle = Occupy(m_sprites, AssetType::Sprite, id, std::move(data));
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
            || FindSlot(m_sprites, handle, AssetType::Sprite) != nullptr;
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
        return false;
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
