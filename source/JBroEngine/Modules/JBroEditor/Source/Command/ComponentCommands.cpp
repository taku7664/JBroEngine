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
        if (false == FindComponentOrdinal(*object, *component, m_address.ordinal))
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
        // **다시 붙은 자리는 맨 끝이다.** 원래 가운데 있었다면 자리가 달라진다 -
        // 기존 엔진도 그렇다(되살리기는 붙이기이지 끼워 넣기가 아니다). 다시하기가
        // 엉뚱한 것을 떼지 않도록 지금 자리를 다시 적어 둔다.
        FindComponentOrdinal(*object, *component, m_address.ordinal);
    }

    void RemoveComponentCommand::Redo()
    {
        Detach();
    }
}
