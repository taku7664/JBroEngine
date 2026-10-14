#include <JBro/Runtime/ComponentHandleReflection.h>

#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Int.h>

#include <charconv>
#include <cstdint>
#include <cstring>

namespace JBro::Internal
{
    namespace
    {
        Bool Emit(const char* local, std::size_t length, char* buffer, std::size_t capacity, std::size_t& required)
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

        // 오브젝트에 붙은 `typeId` 컴포넌트 중 몇 번째인가. 붙어 있지 않으면 -1 이다.
        Int64 OrdinalOf(const Object::GameObject& owner, ComponentTypeId typeId, const ComponentBase* component)
        {
            Int64 ordinal = 0;
            for (const ComponentSlot& slot : owner.GetComponents())
            {
                if (slot.typeId != typeId)
                {
                    continue;
                }
                if (slot.reference.TryGet() == component)
                {
                    return ordinal;
                }
                ++ordinal;
            }
            return -1;
        }

        ComponentBase* NthOfType(const Object::GameObject& owner, ComponentTypeId typeId, Int64 ordinal)
        {
            Int64 seen = 0;
            for (const ComponentSlot& slot : owner.GetComponents())
            {
                if (slot.typeId != typeId)
                {
                    continue;
                }
                if (seen == ordinal)
                {
                    return slot.reference.TryGet();
                }
                ++seen;
            }
            return nullptr;
        }

        // `왼쪽:오른쪽` 두 수다. 글자 전체가 그 모양이어야 한다.
        template<typename TNumber>
        Bool ParsePair(const char* text, const char* end, TNumber& left, TNumber& right)
        {
            const auto first = std::from_chars(text, end, left);
            if (first.ec != std::errc() || first.ptr == end || *first.ptr != ':')
            {
                return false;
            }
            const auto second = std::from_chars(first.ptr + 1, end, right);
            return second.ec == std::errc() && second.ptr == end;
        }
    }

    Bool ComponentHandleToText(const ComponentHandle& handle, ComponentTypeId typeId,
        char* buffer, std::size_t capacity, std::size_t& required) noexcept
    {
        const InstanceRef& reference = ComponentHandleAccess::GetReference(handle);
        char local[48];
        std::size_t length = 0;
        if (reference.ComponentId != InvalidInstanceId)
        {
            const ObjectRefRemap* remap = InstanceRegistry::Get().GetObjectRefRemap();
            if (remap != nullptr && remap->toIndex != nullptr)
            {
                // 파일에는 오브젝트의 파일 안 번호와 같은 타입 중 몇 번째인지를 적는다. 떼였거나 이 파일에 없는 오브젝트면 빈 참조다.
                const ComponentBase* component = ComponentHandleAccess::Resolve(handle);
                const Object::GameObject* owner = component != nullptr
                    ? GameObjectHandleAccess::Resolve(GameObjectHandleAccess::FromId(reference.ObjectId))
                    : nullptr;
                const Int64 index = owner != nullptr ? remap->toIndex(remap->user, owner->GetInstanceId()) : Int64(-1);
                const Int64 ordinal = index >= 0 ? OrdinalOf(*owner, typeId, component) : Int64(-1);
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

    Bool ComponentHandleFromText(ComponentHandle& handle, ComponentTypeId typeId,
        const char* text, std::size_t length) noexcept
    {
        if (text == nullptr)
        {
            return false;
        }
        const char* end = text + length;
        if (length == 0)
        {
            ComponentHandleAccess::SetReference(handle, InstanceRef{});
            return true;
        }
        if (text[0] == '@')
        {
            std::uint64_t objectId = 0;
            std::uint64_t componentId = 0;
            if (false == ParsePair(text + 1, end, objectId, componentId))
            {
                return false;
            }
            InstanceRef reference;
            reference.ObjectId = objectId;
            reference.ComponentId = componentId;
            // 지금 살아 있는 것이 다른 타입이면 읽지 않는다. 없는 것은 받는다 - 되돌리기가 같은 번호로 되살린다.
            const ResolvedInstance resolved = ResolveInstanceById(reference.ObjectId, reference.ComponentId, RefCategory::Component);
            if (resolved.Pointer != nullptr && static_cast<const ComponentBase*>(resolved.Pointer)->GetTypeId() != typeId)
            {
                return false;
            }
            ComponentHandleAccess::SetReference(handle, reference);
            return true;
        }
        // 파일 안 번호는 캔버스 파일을 읽는 동안에만 뜻이 있다.
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
        InstanceRef reference;
        const Object::GameObject* owner = GameObjectHandleAccess::Resolve(
            GameObjectHandleAccess::FromId(remap->toObjectId(remap->user, index)));
        const ComponentBase* component = owner != nullptr && ordinal >= 0 ? NthOfType(*owner, typeId, ordinal) : nullptr;
        if (component != nullptr)
        {
            reference.ObjectId = owner->GetInstanceId();
            reference.ComponentId = component->GetInstanceId();
            reference.Cached = component->GetHandle();
        }
        ComponentHandleAccess::SetReference(handle, reference);
        return true;
    }
}
