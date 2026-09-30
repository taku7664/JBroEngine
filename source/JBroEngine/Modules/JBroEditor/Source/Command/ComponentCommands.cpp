#include <JBro/Editor/Command/ComponentCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptRegistry.h>

namespace JBro
{
    bool FindAttachableKind(NameId typeName, AttachedKind& kind, ComponentTypeId& typeId)
    {
        if (typeName == InvalidNameId)
        {
            return false;
        }
        if (const ComponentTypeInfo* builtin = ComponentRegistry::Get().Find(typeName))
        {
            kind = AttachedKind::Component;
            typeId = builtin->typeId;
            return true;
        }
        if (const ScriptTypeInfo* script = ScriptRegistry::Get().Find(typeName))
        {
            kind = AttachedKind::Script;
            typeId = script->typeId;
            return true;
        }
        return false;
    }

    AttachedRef AttachByName(Canvas& canvas, Object::GameObject& object, NameId typeName)
    {
        if (const ComponentTypeInfo* builtin = ComponentRegistry::Get().Find(typeName))
        {
            return builtin->Attach != nullptr ? AttachedRef(builtin->Attach(canvas, &object)) : AttachedRef{};
        }
        if (ScriptRegistry::Get().Find(typeName) != nullptr)
        {
            return canvas.AttachScript(&object, typeName);
        }
        return {};
    }

    bool DetachAttached(Canvas& canvas, Object::GameObject& object, AttachedRef attached)
    {
        if (attached.script != nullptr)
        {
            return canvas.DetachScript(&object, attached.script);
        }
        if (attached.component == nullptr)
        {
            return false;
        }
        const ComponentTypeInfo* builtin = ComponentRegistry::Get().Find(attached.component->GetTypeId());
        return builtin != nullptr && builtin->Detach != nullptr && builtin->Detach(canvas, &object, attached.component);
    }

    bool CanAttachByName(const Object::GameObject& object, NameId typeName)
    {
        if (ComponentRegistry::Get().Find(typeName) != nullptr)
        {
            return ComponentRegistry::Get().CanAttach(object, typeName);
        }
        // 스크립트는 여럿 붙는다.
        return ScriptRegistry::Get().Find(typeName) != nullptr;
    }

    bool SetAttachedIndex(Object::GameObject& object, AttachedRef attached, std::size_t index)
    {
        if (attached.script != nullptr)
        {
            return object.SetScriptIndex(attached.script, index);
        }
        return object.SetComponentIndex(attached.component, index);
    }

    // -- AddComponentCommand -------------------------------------------------

    AddComponentCommand::AddComponentCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        NameId typeName)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_typeName(typeName)
    {
        m_address.objectId = objectId;
        FindAttachableKind(typeName, m_address.kind, m_address.typeId);
    }

    AddComponentCommand::AddComponentCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        NameId typeName,
        const ComponentSnapshot& values)
        : AddComponentCommand(canvas, registry, objectId, typeName)
    {
        m_values = values;
        m_hasValues = true;
    }

    const char* AddComponentCommand::GetName() const
    {
        if (m_address.kind == AttachedKind::Script)
        {
            return m_hasValues ? "Paste Script" : "Add Script";
        }
        return m_hasValues ? "Paste Component" : "Add Component";
    }

    bool AddComponentCommand::Attach()
    {
        AttachedKind kind = AttachedKind::Component;
        ComponentTypeId typeId = InvalidComponentTypeId;
        Object::GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == FindAttachableKind(m_typeName, kind, typeId) || object == nullptr)
        {
            return false;
        }
        // **하나만 붙는 타입을 두 번 붙이지 않는다**(D-180). 목록이 이미 막고 있지만,
        // 붙여넣기도 다시하기도 이 길을 지나므로 판정은 여기에 있어야 한다. 둘씩 붙으면
        // 조회가 먼저 붙은 쪽만 돌려주어, 나중에 붙은 것은 보이지도 지워지지도 않는다.
        if (false == CanAttachByName(*object, m_typeName))
        {
            return false;
        }
        const AttachedRef attached = AttachByName(*m_canvas, *object, m_typeName);
        if (false == static_cast<bool>(attached))
        {
            return false;
        }
        // 붙은 자리를 세어서 적는다. 지금은 늘 맨 끝이지만, 짐작한 값을 적어 두면
        // 붙이는 쪽이 언젠가 순서를 정하게 될 때 조용히 틀린다.
        m_address.kind = kind;
        m_address.typeId = typeId;
        if (false == FindAttachedOrdinal(*object, attached, m_address.ordinal))
        {
            return false;
        }
        // **값을 못 써 넣으면 붙인 것도 도로 뗀다**(D-167). 반쪽만 붙은 것을 두면
        // 붙여넣기가 성공했다고 말하면서 기본값짜리를 남긴다.
        if (m_hasValues && false == ApplyAttached(attached, m_values))
        {
            DetachAttached(*m_canvas, *object, attached);
            return false;
        }
        m_added = true;
        return true;
    }

    bool AddComponentCommand::Execute()
    {
        return Attach();
    }

    void AddComponentCommand::Undo()
    {
        Object::GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == m_added || object == nullptr)
        {
            return;
        }
        const AttachedRef attached = FindAttachedAt(*object, m_address.kind, m_address.typeId, m_address.ordinal);
        if (attached && DetachAttached(*m_canvas, *object, attached))
        {
            m_added = false;
        }
    }

    void AddComponentCommand::Redo()
    {
        Attach();
    }

    AttachedRef AddComponentCommand::GetAttached() const
    {
        Object::GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == m_added || object == nullptr)
        {
            return {};
        }
        return FindAttachedAt(*object, m_address.kind, m_address.typeId, m_address.ordinal);
    }

    ComponentBase* AddComponentCommand::GetComponent() const
    {
        return GetAttached().component;
    }

    // -- RemoveComponentCommand ----------------------------------------------

    RemoveComponentCommand::RemoveComponentCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        AttachedRef attached)
        : m_canvas(&canvas)
        , m_registry(&registry)
    {
        m_address.objectId = objectId;
        Object::GameObject* object = registry.Resolve(objectId);
        if (object == nullptr || false == static_cast<bool>(attached))
        {
            return;
        }
        m_address.kind = attached.GetKind();
        m_address.typeId = attached.GetTypeId();
        if (const char* typeName = NameTable::Get().Resolve(m_address.typeId))
        {
            m_typeName = NameTable::Get().Intern(typeName);
        }
        if (false == FindAttachedOrdinal(*object, attached, m_address.ordinal)
            || false == FindAttachedIndex(*object, attached, m_slotIndex))
        {
            return;
        }
        m_captured = CaptureAttached(attached, m_snapshot);
    }

    const char* RemoveComponentCommand::GetName() const
    {
        return m_address.kind == AttachedKind::Script ? "Remove Script" : "Remove Component";
    }

    bool RemoveComponentCommand::Detach()
    {
        Object::GameObject* object = m_registry->Resolve(m_address.objectId);
        if (object == nullptr)
        {
            return false;
        }
        const AttachedRef attached = FindAttachedAt(*object, m_address.kind, m_address.typeId, m_address.ordinal);
        if (false == static_cast<bool>(attached))
        {
            return false;
        }
        return DetachAttached(*m_canvas, *object, attached);
    }

    bool RemoveComponentCommand::Execute()
    {
        if (false == m_captured)
        {
            // 값을 못 떴다. 되살릴 수 없는 것은 떼지 않는다.
            return false;
        }
        return Detach();
    }

    void RemoveComponentCommand::Undo()
    {
        Object::GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == m_captured || object == nullptr)
        {
            return;
        }
        const AttachedRef attached = AttachByName(*m_canvas, *object, m_typeName);
        if (false == static_cast<bool>(attached))
        {
            return;
        }
        ApplyAttached(attached, m_snapshot);
        // **다시 붙은 자리는 맨 끝이므로 원래 자리로 보낸다.** 기존 엔진은 맨 끝에
        // 두었는데 거기서는 커맨드가 GUID 로 가리켜서 괜찮았다. 여기서는 "같은 타입 중
        // 몇 번째" 로 가리키므로, 자리가 바뀌면 앞서 쌓인 편집이 형제에게 쏟아진다
        // (실제로 그렇게 났다).
        //
        // "몇 번째" 는 다시 세지 않는다. 되돌리기는 차례대로만 오므로 지금 오브젝트는
        // 커맨드를 만들 때에서 이것만 빠진 상태이고, 같은 슬롯에 끼우면 앞에
        // 선 같은 타입의 수도 같다 - 다시 세어도 처음 센 값이 나온다.
        SetAttachedIndex(*object, attached, m_slotIndex);
    }

    void RemoveComponentCommand::Redo()
    {
        Detach();
    }

    // -- PasteComponentValuesCommand -----------------------------------------

    PasteComponentValuesCommand::PasteComponentValuesCommand(
        EditorObjectRegistry& registry,
        const ComponentAddress& address,
        const ComponentSnapshot& values)
        : m_registry(&registry)
        , m_address(address)
        , m_values(values)
    {
        const AttachedRef attached = ResolveAttached(registry, address);
        if (false == static_cast<bool>(attached))
        {
            return;
        }
        m_captured = CaptureAttached(attached, m_before);
    }

    const char* PasteComponentValuesCommand::GetName() const
    {
        return m_address.kind == AttachedKind::Script ? "Paste Script Values" : "Paste Component Values";
    }

    bool PasteComponentValuesCommand::Apply(const ComponentSnapshot& values)
    {
        const AttachedRef attached = ResolveAttached(*m_registry, m_address);
        if (false == static_cast<bool>(attached))
        {
            return false;
        }
        return ApplyAttached(attached, values);
    }

    bool PasteComponentValuesCommand::Execute()
    {
        if (false == m_captured || m_values.typeId != m_address.typeId || m_values.kind != m_address.kind)
        {
            // 덮기 전 값을 못 떴거나, 떠 둔 것이 다른 타입이다. 둘 다 덮지 않는다.
            return false;
        }
        return Apply(m_values);
    }

    void PasteComponentValuesCommand::Undo()
    {
        Apply(m_before);
    }

    void PasteComponentValuesCommand::Redo()
    {
        Apply(m_values);
    }

    // -- MoveComponentCommand ------------------------------------------------

    MoveComponentCommand::MoveComponentCommand(
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        std::size_t fromSlot,
        std::size_t toSlot,
        AttachedKind kind)
        : m_registry(&registry)
        , m_objectId(objectId)
        , m_from(fromSlot)
        , m_to(toSlot)
        , m_kind(kind)
    {
    }

    const char* MoveComponentCommand::GetName() const
    {
        return m_kind == AttachedKind::Script ? "Move Script" : "Move Component";
    }

    bool MoveComponentCommand::Move(std::size_t from, std::size_t to)
    {
        Object::GameObject* object = m_registry->Resolve(m_objectId);
        if (object == nullptr || from == to)
        {
            return false;
        }
        if (m_kind == AttachedKind::Script)
        {
            const Array<ScriptSlot>& slots = object->GetScripts();
            if (from >= slots.Size() || to >= slots.Size())
            {
                return false;
            }
            GameScriptBase* script = slots[from].reference.TryGet();
            return script != nullptr && object->SetScriptIndex(script, to);
        }
        const Array<ComponentSlot>& slots = object->GetComponents();
        if (from >= slots.Size() || to >= slots.Size())
        {
            return false;
        }
        ComponentBase* component = slots[from].reference.TryGet();
        if (component == nullptr)
        {
            return false;
        }
        return object->SetComponentIndex(component, to);
    }

    bool MoveComponentCommand::Execute()
    {
        return Move(m_from, m_to);
    }

    void MoveComponentCommand::Undo()
    {
        Move(m_to, m_from);
    }

    void MoveComponentCommand::Redo()
    {
        Move(m_from, m_to);
    }
}
