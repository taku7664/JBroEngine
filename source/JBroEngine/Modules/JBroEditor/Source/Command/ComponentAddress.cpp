#include <JBro/Editor/Command/ComponentAddress.h>

#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    void* AttachedRef::GetInstance() const
    {
        if (script != nullptr)
        {
            return script;
        }
        return component;
    }

    ComponentTypeId AttachedRef::GetTypeId() const
    {
        if (script != nullptr)
        {
            return script->GetTypeId();
        }
        return component != nullptr ? component->GetTypeId() : InvalidComponentTypeId;
    }

    bool AttachedRef::IsEnabled() const
    {
        if (script != nullptr)
        {
            return script->IsEnabled();
        }
        return component != nullptr && component->IsEnabled();
    }

    void AttachedRef::SetEnabled(bool enabled) const
    {
        if (script != nullptr)
        {
            script->SetEnabled(enabled);
            return;
        }
        if (component != nullptr)
        {
            component->SetEnabled(enabled);
        }
    }

    bool ComponentAddress::Equals(const ComponentAddress& other) const
    {
        return objectId == other.objectId
            && kind == other.kind
            && typeId == other.typeId
            && ordinal == other.ordinal;
    }

    AttachedRef FindAttachedAt(Object::GameObject& object, AttachedKind kind, ComponentTypeId typeId,
        std::uint32_t ordinal)
    {
        if (kind == AttachedKind::Component)
        {
            return FindComponentAt(object, typeId, ordinal);
        }
        std::uint32_t seen = 0;
        for (const ScriptSlot& slot : object.GetScripts())
        {
            if (slot.typeId != typeId)
            {
                continue;
            }
            GameScriptBase* script = slot.reference.TryGet();
            if (script == nullptr)
            {
                // 죽은 슬롯은 세지 않는다. 세면 그 뒤의 번호가 한 칸씩 밀린다.
                continue;
            }
            if (seen == ordinal)
            {
                return script;
            }
            ++seen;
        }
        return {};
    }

    bool FindAttachedOrdinal(const Object::GameObject& object, AttachedRef attached, std::uint32_t& ordinal)
    {
        if (attached.script == nullptr)
        {
            return attached.component != nullptr && FindComponentOrdinal(object, *attached.component, ordinal);
        }
        const ComponentTypeId typeId = attached.script->GetTypeId();
        std::uint32_t seen = 0;
        for (const ScriptSlot& slot : object.GetScripts())
        {
            GameScriptBase* candidate = slot.reference.TryGet();
            if (candidate == nullptr || slot.typeId != typeId)
            {
                continue;
            }
            if (candidate == attached.script)
            {
                ordinal = seen;
                return true;
            }
            ++seen;
        }
        return false;
    }

    AttachedRef ResolveAttached(const EditorObjectRegistry& registry, const ComponentAddress& address)
    {
        Object::GameObject* object = registry.Resolve(address.objectId);
        if (object == nullptr)
        {
            return {};
        }
        return FindAttachedAt(*object, address.kind, address.typeId, address.ordinal);
    }

    bool MakeAttachedAddress(EditorObjectRegistry& registry, Object::GameObject& object, AttachedRef attached,
        ComponentAddress& address)
    {
        std::uint32_t ordinal = 0;
        if (false == FindAttachedOrdinal(object, attached, ordinal))
        {
            return false;
        }
        const EditorObjectId id = registry.Track(&object);
        if (id == InvalidEditorObjectId)
        {
            return false;
        }
        address.objectId = id;
        address.kind = attached.GetKind();
        address.typeId = attached.GetTypeId();
        address.ordinal = ordinal;
        return true;
    }

    bool FindAttachedIndex(const Object::GameObject& object, AttachedRef attached, std::size_t& index)
    {
        if (attached.script != nullptr)
        {
            return object.FindScriptIndex(attached.script, index);
        }
        return object.FindComponentIndex(attached.component, index);
    }

    ComponentBase* FindComponentAt(Object::GameObject& object, ComponentTypeId typeId,
        std::uint32_t ordinal)
    {
        std::uint32_t seen = 0;
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

    bool FindComponentOrdinal(const Object::GameObject& object, const ComponentBase& component,
        std::uint32_t& ordinal)
    {
        std::uint32_t seen = 0;
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
        Object::GameObject* object = registry.Resolve(address.objectId);
        if (object == nullptr || address.kind != AttachedKind::Component)
        {
            return nullptr;
        }
        return FindComponentAt(*object, address.typeId, address.ordinal);
    }

    bool MakeComponentAddress(EditorObjectRegistry& registry, Object::GameObject& object,
        const ComponentBase& component, ComponentAddress& address)
    {
        std::uint32_t ordinal = 0;
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
        address.kind = AttachedKind::Component;
        address.typeId = component.GetTypeId();
        address.ordinal = ordinal;
        return true;
    }
}
