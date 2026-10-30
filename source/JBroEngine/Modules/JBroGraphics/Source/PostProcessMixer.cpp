#include <JBro/Graphics/PostProcessMixer.h>

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Types/NameTable.h>

#include <cmath>
#include <cstring>
#include <string_view>

namespace JBro
{
    namespace
    {
        // 16 바이트 칸에 잇는다(렌더러의 블록 복사와 같다).
        std::size_t Align16(std::size_t value)
        {
            return (value + 15u) & ~static_cast<std::size_t>(15u);
        }

        // `Float` 만 든 구조인가 - 필드마다 보간한다.
        Bool HoldsOnlyFloats(const TypeDescriptor& type)
        {
            if (&type == &TypeDescriptorOf<Float>::Get())
            {
                return true;
            }
            if (type.fields == nullptr || type.fields->count == 0)
            {
                return false;
            }
            for (UInt32 at = 0; at < type.fields->count; ++at)
            {
                if (false == HoldsOnlyFloats(*type.fields->properties[at.Get()].type))
                {
                    return false;
                }
            }
            return true;
        }

        // 필드 하나를 `weight` 만큼 `source` 쪽으로 옮긴다. 바꾸는(보간하지 않는) 필드를 실제로 바꿨으면 참이다.
        Bool MixValue(const TypeDescriptor& type, void* target, const void* source, Float weight)
        {
            if (&type == &TypeDescriptorOf<Float>::Get())
            {
                Float& value = *static_cast<Float*>(target);
                value = value + (*static_cast<const Float*>(source) - value) * weight;
                return false;
            }
            if (&type == &TypeDescriptorOf<Int32>::Get())
            {
                Int32& value = *static_cast<Int32*>(target);
                const double mixed = static_cast<double>(value.Get())
                    + (static_cast<double>(static_cast<const Int32*>(source)->Get()) - static_cast<double>(value.Get())) * weight.Get();
                value = static_cast<std::int32_t>(std::lround(mixed));
                return false;
            }
            if (HoldsOnlyFloats(type))
            {
                for (UInt32 at = 0; at < type.fields->count; ++at)
                {
                    const PropertyInfo& field = type.fields->properties[at.Get()];
                    MixValue(*field.type, field.Address(target), field.ConstAddress(source), weight);
                }
                return false;
            }
            if (weight < 0.5f)
            {
                return false;
            }
            std::memcpy(target, source, type.size.Get());
            return true;
        }
    }

    void PostProcessMixer::Reserve(UInt32 entries, UInt32 blockBytes)
    {
        m_entries.Reserve(entries.Get());
        if (m_blocks.Size() < blockBytes.Get())
        {
            m_blocks.Resize(blockBytes.Get());
        }
    }

    std::byte* PostProcessMixer::FindBlock(UInt32 type, Bool create)
    {
        for (const PostProcessEntry& entry : m_entries)
        {
            if (entry.type == type)
            {
                return m_blocks.Data() + entry.blockOffset.Get();
            }
        }
        const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(type);
        if (false == create || effect == nullptr)
        {
            return nullptr;
        }
        const std::size_t at = Align16(m_used.Get());
        if (m_entries.Size() >= m_entries.Capacity() || at + effect->blockSize.Get() > m_blocks.Size())
        {
            ++m_dropped;
            return nullptr;
        }
        if (effect->blockSize != 0)
        {
            std::memcpy(m_blocks.Data() + at, effect->defaults, effect->blockSize.Get());
        }
        PostProcessEntry entry;
        entry.type = type;
        entry.blockOffset = static_cast<JBro::UInt32>(at);
        m_entries.Add(entry);
        m_used = static_cast<JBro::UInt32>(at + effect->blockSize.Get());
        return m_blocks.Data() + at;
    }

    void PostProcessMixer::Begin(const PostProcessStack& base)
    {
        m_entries.Clear();
        m_used = 0;
        m_dropped = 0;
        for (UInt32 index = 0; index < base.entries.size; ++index)
        {
            const PostProcessEntry& entry = base.entries.data[index.Get()];
            const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(entry.type);
            if (false == entry.enabled || effect == nullptr)
            {
                continue;
            }
            const std::size_t at = Align16(m_used.Get());
            if (m_entries.Size() >= m_entries.Capacity() || at + effect->blockSize.Get() > m_blocks.Size()
                || static_cast<std::size_t>(entry.blockOffset.Get()) + effect->blockSize.Get() > base.blocks.size.Get())
            {
                ++m_dropped;
                continue;
            }
            if (effect->blockSize != 0)
            {
                std::memcpy(m_blocks.Data() + at, base.blocks.data + entry.blockOffset.Get(), effect->blockSize.Get());
            }
            PostProcessEntry copy;
            copy.type = entry.type;
            copy.blockOffset = static_cast<JBro::UInt32>(at);
            m_entries.Add(copy);
            m_used = static_cast<JBro::UInt32>(at + effect->blockSize.Get());
        }
    }

    void PostProcessMixer::Blend(const PostProcessProfileView& profile, Float weight)
    {
        // 유한하지 않은 무게(NaN)는 섞지 않는다 - 비교가 거짓이다.
        if (false == (weight > 0.0f))
        {
            return;
        }
        const Float w = weight > 1.0f ? Float(1.0f) : weight;
        for (UInt32 index = 0; index < profile.stack.entries.size && index < profile.overrides.size; ++index)
        {
            const PostProcessEntry& entry = profile.stack.entries.data[index.Get()];
            const UInt64 overrides = profile.overrides.data[index.Get()];
            const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(entry.type);
            if (false == entry.enabled || effect == nullptr || effect->parameters == nullptr || overrides == 0
                || static_cast<std::size_t>(entry.blockOffset.Get()) + effect->blockSize.Get() > profile.stack.blocks.size.Get())
            {
                continue;
            }
            std::byte* target = FindBlock(entry.type, true);
            if (target == nullptr)
            {
                continue;
            }
            const std::byte* source = profile.stack.blocks.data + entry.blockOffset.Get();
            // 블록을 스스로 섞는 종류(머티리얼의 파라미터, D-348)는 그 함수가 섞는다.
            if (effect->mix != nullptr && effect->mix(target, source, overrides, w))
            {
                continue;
            }
            const PropertyTable& table = *effect->parameters;
            Bool assetSwitched = false;
            for (UInt32 field = 0; field < table.count && field < 64; ++field)
            {
                if (((overrides >> field.Get()) & 1u) == 0)
                {
                    continue;
                }
                const PropertyInfo& property = table.properties[field.Get()];
                const Bool switched = MixValue(*property.type, property.Address(target), property.ConstAddress(source), w);
                if (switched && property.type == &TypeDescriptorOf<AssetId>::Get())
                {
                    assetSwitched = true;
                }
            }
            // 풀린 값(저장하지 않는 필드)은 그 항목의 에셋이 바뀔 때 함께 바뀐다 - LUT 의 렌더러 텍스처가 아이디를 따라간다.
            if (assetSwitched)
            {
                for (UInt32 field = 0; field < table.count; ++field)
                {
                    const PropertyInfo& property = table.properties[field.Get()];
                    if (false == property.serialize)
                    {
                        std::memcpy(property.Address(target), property.ConstAddress(source), property.type->size.Get());
                    }
                }
            }
        }
    }

    void PostProcessMixer::ApplyOverrides()
    {
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            const ScriptOverride& script = m_overrides[at.Get()];
            const UInt32 type = script.field >> 16;
            const UInt32 index = script.field & 0xFFFFu;
            const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(type);
            if (effect == nullptr || effect->parameters == nullptr || index >= effect->parameters->count)
            {
                continue;
            }
            std::byte* target = FindBlock(type, true);
            if (target == nullptr)
            {
                continue;
            }
            const PropertyInfo& property = effect->parameters->properties[index.Get()];
            std::memcpy(property.Address(target), script.value, property.type->size.Get());
        }
    }

    PostProcessStack PostProcessMixer::GetResult() const
    {
        return {{m_entries.Data(), static_cast<JBro::UInt32>(m_entries.Size())}, {m_blocks.Data(), m_used}};
    }

    UInt32 PostProcessMixer::FindField(const char* effect, const char* field)
    {
        const UInt32 type = PostProcessEffectRegistry::Find(effect);
        const PostProcessEffectType* found = PostProcessEffectRegistry::Get(type);
        if (found == nullptr || found->parameters == nullptr || field == nullptr || type > 0xFFFFu)
        {
            return InvalidField;
        }
        const std::string_view wanted(field);
        for (UInt32 index = 0; index < found->parameters->count; ++index)
        {
            const PropertyInfo& property = found->parameters->properties[index.Get()];
            if (NameTable::Get().Resolve(property.name) == wanted)
            {
                // 숨긴 값(렌더러 핸들 따위)은 스크립트도 덮어쓰지 못한다.
                return property.serialize && property.type->size <= MaxOverrideBytes ? (type << 16) | index : InvalidField;
            }
        }
        return InvalidField;
    }

    Bool PostProcessMixer::SetOverride(UInt32 field, const TypeDescriptor& type, const void* value)
    {
        const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(field >> 16);
        const UInt32 index = field & 0xFFFFu;
        if (field == InvalidField || effect == nullptr || effect->parameters == nullptr || index >= effect->parameters->count
            || effect->parameters->properties[index.Get()].type != &type || type.size > MaxOverrideBytes || value == nullptr)
        {
            return false;
        }
        ScriptOverride* slot = nullptr;
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            if (m_overrides[at.Get()].field == field)
            {
                slot = &m_overrides[at.Get()];
                break;
            }
        }
        if (slot == nullptr)
        {
            if (m_overrideCount >= MaxScriptOverrides)
            {
                return false;
            }
            slot = &m_overrides[m_overrideCount.Get()];
            ++m_overrideCount;
        }
        slot->field = field;
        std::memcpy(slot->value, value, type.size.Get());
        return true;
    }

    void PostProcessMixer::ClearOverride(UInt32 field)
    {
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            if (m_overrides[at.Get()].field == field)
            {
                m_overrides[at.Get()] = m_overrides[(m_overrideCount - 1).Get()];
                --m_overrideCount;
                return;
            }
        }
    }

    void PostProcessMixer::CopyOverridesFrom(const PostProcessMixer& other)
    {
        if (&other == this)
        {
            return;
        }
        for (UInt32 index = 0; index < other.m_overrideCount; ++index)
        {
            m_overrides[index.Get()] = other.m_overrides[index.Get()];
        }
        m_overrideCount = other.m_overrideCount;
    }

    void PostProcessMixer::ClearOverrides()
    {
        m_overrideCount = 0;
    }
}
