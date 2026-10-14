#include <JBro/Runtime/ScriptRefReflection.h>

#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

#include <charconv>
#include <cstdint>
#include <cstring>

namespace JBro::Internal
{
    namespace
    {
        Bool Emit(const char* local, std::size_t length, char* buffer,
            std::size_t capacity, std::size_t& required) noexcept
        {
            required = length + 1;
            if (buffer == nullptr || capacity < required)
            {
                return false;
            }
            std::memcpy(buffer, local, length);
            buffer[length] = '\0';
            return true;
        }

        template<typename TNumber>
        Bool ParsePair(const char* text, const char* end, TNumber& left, TNumber& right) noexcept
        {
            const auto first = std::from_chars(text, end, left);
            if (first.ec != std::errc() || first.ptr == end || *first.ptr != ':')
            {
                return false;
            }
            const auto second = std::from_chars(first.ptr + 1, end, right);
            return second.ec == std::errc() && second.ptr == end;
        }

        Int64 OrdinalOf(const Object::GameObject& owner, ComponentTypeId typeId,
            const GameScriptBase* script) noexcept
        {
            Int64 ordinal = 0;
            for (const ScriptSlot& slot : owner.GetScripts())
            {
                if (slot.typeId == typeId)
                {
                    if (slot.reference.TryGet() == script)
                    {
                        return ordinal;
                    }
                    ++ordinal;
                }
            }
            return -1;
        }

        GameScriptBase* NthOfType(const Object::GameObject& owner,
            ComponentTypeId typeId, Int64 ordinal) noexcept
        {
            Int64 seen = 0;
            for (const ScriptSlot& slot : owner.GetScripts())
            {
                if (slot.typeId == typeId)
                {
                    if (seen == ordinal)
                    {
                        return slot.reference.TryGet();
                    }
                    ++seen;
                }
            }
            return nullptr;
        }
    }

    Bool ScriptRefToText(const InstanceRef& reference, ComponentTypeId typeId,
        char* buffer, std::size_t capacity, std::size_t& required) noexcept
    {
        char local[48];
        std::size_t length = 0;
        if (reference.ComponentId != InvalidInstanceId)
        {
            const ObjectRefRemap* remap = InstanceRegistry::Get().GetObjectRefRemap();
            if (remap != nullptr && remap->toIndex != nullptr)
            {
                const ResolvedInstance resolved = ResolveInstanceById(
                    reference.ObjectId, reference.ComponentId, RefCategory::Script);
                const auto* script = static_cast<const GameScriptBase*>(resolved.Pointer);
                const Object::GameObject* owner = script != nullptr
                    ? GameObjectHandleAccess::Resolve(GameObjectHandleAccess::FromId(reference.ObjectId))
                    : nullptr;
                const Int64 index = owner != nullptr
                    ? remap->toIndex(remap->user, owner->GetInstanceId()) : Int64(-1);
                const Int64 ordinal = index >= 0 && script->GetCachedTypeId() == typeId
                    ? OrdinalOf(*owner, typeId, script) : Int64(-1);
                if (ordinal >= 0)
                {
                    char* at = std::to_chars(local, local + sizeof(local), index.Get()).ptr;
                    *at++ = ':';
                    at = std::to_chars(at, local + sizeof(local), ordinal.Get()).ptr;
                    length = static_cast<std::size_t>(at - local);
                }
            }
            else
            {
                char* at = local;
                *at++ = '@';
                at = std::to_chars(at, local + sizeof(local), reference.ObjectId.Get()).ptr;
                *at++ = ':';
                at = std::to_chars(at, local + sizeof(local), reference.ComponentId.Get()).ptr;
                length = static_cast<std::size_t>(at - local);
            }
        }
        return Emit(local, length, buffer, capacity, required);
    }

    Bool ScriptRefFromText(InstanceRef& destination, ComponentTypeId typeId,
        const char* text, std::size_t length) noexcept
    {
        if (text == nullptr)
        {
            return false;
        }
        if (length == 0)
        {
            destination = {};
            return true;
        }
        const char* end = text + length;
        if (text[0] == '@')
        {
            std::uint64_t objectId = 0;
            std::uint64_t scriptId = 0;
            if (false == ParsePair(text + 1, end, objectId, scriptId))
            {
                return false;
            }
            InstanceRef parsed;
            parsed.ObjectId = objectId;
            parsed.ComponentId = scriptId;
            const ResolvedInstance resolved = ResolveInstanceById(
                parsed.ObjectId, parsed.ComponentId, RefCategory::Script);
            if (resolved.Pointer != nullptr
                && static_cast<const GameScriptBase*>(resolved.Pointer)->GetCachedTypeId() != typeId)
            {
                return false;
            }
            destination = parsed;
            return true;
        }
        const ObjectRefRemap* remap = InstanceRegistry::Get().GetObjectRefRemap();
        if (remap == nullptr || remap->toObjectId == nullptr)
        {
            return false;
        }
        std::int64_t index = -1;
        std::int64_t ordinal = -1;
        if (false == ParsePair(text, end, index, ordinal))
        {
            return false;
        }
        InstanceRef parsed;
        const Object::GameObject* owner = GameObjectHandleAccess::Resolve(
            GameObjectHandleAccess::FromId(remap->toObjectId(remap->user, index)));
        GameScriptBase* script = owner != nullptr && ordinal >= 0
            ? NthOfType(*owner, typeId, ordinal) : nullptr;
        if (script != nullptr)
        {
            parsed.ObjectId = owner->GetInstanceId();
            parsed.ComponentId = script->GetInstanceId();
            parsed.Cached = script->GetHandle();
        }
        destination = parsed;
        return true;
    }
}
