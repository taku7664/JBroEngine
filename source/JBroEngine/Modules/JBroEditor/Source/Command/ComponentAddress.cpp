#include <JBro/Editor/Command/ComponentAddress.h>

#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool ComponentAddress::Equals(const ComponentAddress& other) const
    {
        return objectId == other.objectId
            && typeId == other.typeId
            && ordinal == other.ordinal;
    }

    ComponentBase* FindComponentAt(GameObject& object, ComponentTypeId typeId,
        UInt32 ordinal)
    {
        UInt32 seen = 0;
        const Array<ComponentSlot>& components = object.GetComponents();
        for (std::size_t index = 0; index < components.Size(); ++index)
        {
            if (components[index].typeId != typeId)
            {
                continue;
            }
            ComponentBase* component = components[index].reference.TryGet();
            if (component == nullptr)
            {
                // 죽은 슬롯은 세지 않는다. 세면 그 뒤의 번호가 한 칸씩 밀린다.
                continue;
            }
            if (seen == ordinal)
            {
                return component;
            }
            ++seen;
        }
        return nullptr;
    }

    Bool FindComponentOrdinal(const GameObject& object, const ComponentBase& component,
        UInt32& ordinal)
    {
        UInt32 seen = 0;
        const Array<ComponentSlot>& components = object.GetComponents();
        for (std::size_t index = 0; index < components.Size(); ++index)
        {
            ComponentBase* candidate = components[index].reference.TryGet();
            if (candidate == nullptr || components[index].typeId != component.GetTypeId())
            {
                continue;
            }
            if (candidate == &component)
            {
                ordinal = seen;
                return true;
            }
            ++seen;
        }
        return false;
    }

    ComponentBase* ResolveComponent(const EditorObjectRegistry& registry,
        const ComponentAddress& address)
    {
        GameObject* object = registry.Resolve(address.objectId);
        if (object == nullptr)
        {
            return nullptr;
        }
        return FindComponentAt(*object, address.typeId, address.ordinal);
    }

    Bool MakeComponentAddress(EditorObjectRegistry& registry, GameObject& object,
        const ComponentBase& component, ComponentAddress& address)
    {
        UInt32 ordinal = 0;
        if (false == FindComponentOrdinal(object, component, ordinal))
        {
            return false;
        }
        const EditorObjectId id = registry.Track(&object);
        if (id == InvalidEditorObjectId)
        {
            return false;
        }
        address.objectId = id;
        address.typeId = component.GetTypeId();
        address.ordinal = ordinal;
        return true;
    }
}
