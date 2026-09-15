#include <JBro/Editor/Command/ComponentCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    namespace
    {
        const ComponentTypeInfo* FindType(NameId typeName)
        {
            if (typeName == InvalidNameId)
            {
                return nullptr;
            }
            return ComponentRegistry::Get().Find(typeName);
        }
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
        if (const ComponentTypeInfo* info = FindType(typeName))
        {
            m_address.typeId = info->typeId;
        }
    }

    const char* AddComponentCommand::GetName() const
    {
        return "Add Component";
    }

    bool AddComponentCommand::Attach()
    {
        const ComponentTypeInfo* info = FindType(m_typeName);
        GameObject* object = m_registry->Resolve(m_address.objectId);
        if (info == nullptr || info->Attach == nullptr || object == nullptr)
        {
            return false;
        }
        ComponentBase* component = info->Attach(*m_canvas, object);
        if (component == nullptr)
        {
            return false;
        }
        // 붙은 자리를 세어서 적는다. 지금은 늘 맨 끝이지만, 짐작한 값을 적어 두면
        // 붙이는 쪽이 언젠가 순서를 정하게 될 때 조용히 틀린다.
        if (false == FindComponentOrdinal(*object, *component, m_address.ordinal))
        {
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
        const ComponentTypeInfo* info = FindType(m_typeName);
        GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == m_added || info == nullptr || info->Detach == nullptr
            || object == nullptr)
        {
            return;
        }
        ComponentBase* component =
            FindComponentAt(*object, m_address.typeId, m_address.ordinal);
        if (component == nullptr)
        {
            return;
        }
        if (info->Detach(*m_canvas, object, component))
        {
            m_added = false;
        }
    }

    void AddComponentCommand::Redo()
    {
        Attach();
    }

    ComponentBase* AddComponentCommand::GetComponent() const
    {
        GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == m_added || object == nullptr)
        {
            return nullptr;
        }
        return FindComponentAt(*object, m_address.typeId, m_address.ordinal);
    }

    // -- RemoveComponentCommand ----------------------------------------------

    RemoveComponentCommand::RemoveComponentCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        ComponentBase* component)
        : m_canvas(&canvas)
        , m_registry(&registry)
    {
        m_address.objectId = objectId;
        GameObject* object = registry.Resolve(objectId);
        if (object == nullptr || component == nullptr)
        {
            return;
        }
        m_address.typeId = component->GetTypeId();
        if (const char* typeName = NameTable::Get().Resolve(m_address.typeId))
        {
            m_typeName = NameTable::Get().Intern(typeName);
        }
        if (false == FindComponentOrdinal(*object, *component, m_address.ordinal)
            || false == object->FindComponentIndex(component, m_slotIndex))
        {
            return;
        }
        m_captured = CaptureComponent(*component, m_snapshot);
    }

    const char* RemoveComponentCommand::GetName() const
    {
        return "Remove Component";
    }

    bool RemoveComponentCommand::Detach()
    {
        const ComponentTypeInfo* info = FindType(m_typeName);
        GameObject* object = m_registry->Resolve(m_address.objectId);
        if (info == nullptr || info->Detach == nullptr || object == nullptr)
        {
            return false;
        }
        ComponentBase* component =
            FindComponentAt(*object, m_address.typeId, m_address.ordinal);
        if (component == nullptr)
        {
            return false;
        }
        return info->Detach(*m_canvas, object, component);
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
        const ComponentTypeInfo* info = FindType(m_typeName);
        GameObject* object = m_registry->Resolve(m_address.objectId);
        if (false == m_captured || info == nullptr || info->Attach == nullptr
            || object == nullptr)
        {
            return;
        }
        ComponentBase* component = info->Attach(*m_canvas, object);
        if (component == nullptr)
        {
            return;
        }
        ApplyComponent(*component, m_snapshot);
        // **다시 붙은 자리는 맨 끝이므로 원래 자리로 보낸다.** 기존 엔진은 맨 끝에
        // 두었는데 거기서는 커맨드가 GUID 로 가리켜서 괜찮았다. 여기서는 "같은 타입 중
        // 몇 번째" 로 가리키므로, 자리가 바뀌면 앞서 쌓인 편집이 형제에게 쏟아진다
        // (실제로 그렇게 났다).
        //
        // "몇 번째" 는 다시 세지 않는다. 되돌리기는 차례대로만 오므로 지금 오브젝트는
        // 커맨드를 만들 때에서 이 컴포넌트만 빠진 상태이고, 같은 슬롯에 끼우면 앞에
        // 선 같은 타입의 수도 같다 - 다시 세어도 처음 센 값이 나온다.
        object->SetComponentIndex(component, m_slotIndex);
    }

    void RemoveComponentCommand::Redo()
    {
        Detach();
    }

    // -- MoveComponentCommand ------------------------------------------------

    MoveComponentCommand::MoveComponentCommand(
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        std::size_t fromSlot,
        std::size_t toSlot)
        : m_registry(&registry)
        , m_objectId(objectId)
        , m_from(fromSlot)
        , m_to(toSlot)
    {
    }

    const char* MoveComponentCommand::GetName() const
    {
        return "Move Component";
    }

    bool MoveComponentCommand::Move(std::size_t from, std::size_t to)
    {
        GameObject* object = m_registry->Resolve(m_objectId);
        if (object == nullptr || from == to)
        {
            return false;
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
