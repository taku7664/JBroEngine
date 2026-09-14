#include <JBro/Editor/Command/ObjectCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <utility>

namespace JBro
{
    // ── CreateObjectCommand ──────────────────────────────────────────────────

    CreateObjectCommand::CreateObjectCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        const char* name,
        EditorObjectId parentId)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_name(name != nullptr ? name : "GameObject")
        , m_parentId(parentId)
    {
    }

    const char* CreateObjectCommand::GetName() const
    {
        return "Create Object";
    }

    bool CreateObjectCommand::Create()
    {
        GameObject* object = m_canvas->CreateObject(m_name.c_str());
        if (object == nullptr)
        {
            return false;
        }
        if (m_parentId != InvalidEditorObjectId)
        {
            // 부모가 그 사이에 사라졌으면 뿌리에 둔다. 만들기를 통째로 실패시키면
            // 다시하기가 스택 중간에서 막힌다.
            if (GameObject* parent = m_registry->Resolve(m_parentId))
            {
                object->SetParent(parent);
            }
        }

        if (m_objectId == InvalidEditorObjectId)
        {
            m_objectId = m_registry->Track(object);
            return m_objectId != InvalidEditorObjectId;
        }
        // 다시하기다. **같은 번호에 다시 건다** - 그 번호를 들고 있는 커맨드들이
        // 이 오브젝트를 계속 찾아야 한다.
        return m_registry->Rebind(m_objectId, object);
    }

    bool CreateObjectCommand::Execute()
    {
        return Create();
    }

    void CreateObjectCommand::Undo()
    {
        if (GameObject* object = m_registry->Resolve(m_objectId))
        {
            m_canvas->DestroyObject(object);
            m_canvas->FlushPendingDestroy();
        }
    }

    void CreateObjectCommand::Redo()
    {
        Create();
    }

    EditorObjectId CreateObjectCommand::GetObjectId() const
    {
        return m_objectId;
    }

    // ── DeleteObjectCommand ──────────────────────────────────────────────────

    DeleteObjectCommand::DeleteObjectCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        GameObject* object)
        : m_canvas(&canvas)
        , m_registry(&registry)
    {
        if (object == nullptr)
        {
            return;
        }
        m_parentId = object->GetParent() != nullptr
            ? m_registry->Track(object->GetParent())
            : InvalidEditorObjectId;
        m_captured = Capture(*object, -1);
    }

    const char* DeleteObjectCommand::GetName() const
    {
        return "Delete Object";
    }

    bool DeleteObjectCommand::Capture(GameObject& object, std::int64_t parentIndex)
    {
        ObjectSnapshot snapshot;
        snapshot.id = m_registry->Track(&object);
        const char* name = object.GetTag();
        snapshot.name = name != nullptr ? name : "";
        snapshot.active = object.IsActiveSelf();
        snapshot.parentIndex = parentIndex;

        const Array<ComponentSlot>& components = object.GetComponents();
        for (std::size_t index = 0; index < components.Size(); ++index)
        {
            ComponentBase* component = components[index].reference.TryGet();
            if (component == nullptr)
            {
                continue;
            }
            ComponentSnapshot captured;
            if (false == CaptureComponent(*component, captured))
            {
                // 프로퍼티를 등록하지 않은 타입이다. 되살려 봐야 값이 비어 있으므로
                // 지우는 것을 거절한다 - 조용히 잃는 것보다 낫다.
                return false;
            }
            snapshot.components.Add(std::move(captured));
        }

        const std::int64_t self = static_cast<std::int64_t>(m_objects.Size());
        m_objects.Add(std::move(snapshot));

        const Array<SafePtr<GameObject>>& children = object.GetChildren();
        for (std::size_t index = 0; index < children.Size(); ++index)
        {
            if (GameObject* child = children[index].TryGet())
            {
                if (false == Capture(*child, self))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool DeleteObjectCommand::Restore()
    {
        GameObject* outerParent = m_parentId != InvalidEditorObjectId
            ? m_registry->Resolve(m_parentId)
            : nullptr;

        // 만든 것을 순서대로 들고 있는다. 부모는 늘 먼저 나오므로 앞에서부터
        // 만들면 붙일 자리가 이미 있다.
        Array<GameObject*> created;
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            const ObjectSnapshot& snapshot = m_objects[index];
            GameObject* object = m_canvas->CreateObject(snapshot.name.c_str());
            if (object == nullptr)
            {
                return false;
            }
            GameObject* parent = snapshot.parentIndex < 0
                ? outerParent
                : created[static_cast<std::size_t>(snapshot.parentIndex)];
            if (parent != nullptr)
            {
                object->SetParent(parent);
            }
            object->SetActive(snapshot.active);
            // **옛 번호에 다시 건다.** 이 오브젝트를 가리키던 커맨드들이 계속
            // 찾아야 한다.
            m_registry->Rebind(snapshot.id, object);
            created.Add(object);

            for (std::size_t c = 0; c < snapshot.components.Size(); ++c)
            {
                const ComponentSnapshot& captured = snapshot.components[c];
                const char* typeName = NameTable::Get().Resolve(captured.typeId);
                const ComponentTypeInfo* info = typeName != nullptr
                    ? ComponentRegistry::Get().Find(typeName)
                    : nullptr;
                if (info == nullptr || info->Attach == nullptr)
                {
                    return false;
                }
                ComponentBase* component = info->Attach(*m_canvas, object);
                if (component == nullptr || false == ApplyComponent(*component, captured))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool DeleteObjectCommand::DestroyTracked()
    {
        if (m_objects.IsEmpty())
        {
            return false;
        }
        GameObject* object = m_registry->Resolve(m_objects[0].id);
        if (object == nullptr)
        {
            return false;
        }
        // 자식은 캔버스가 함께 지운다. 스냅샷에는 그 자식들도 들어 있으므로
        // 되살릴 때 나무가 통째로 돌아온다.
        const bool destroyed = m_canvas->DestroyObject(object);
        m_canvas->FlushPendingDestroy();
        return destroyed;
    }

    bool DeleteObjectCommand::Execute()
    {
        if (false == m_captured || m_objects.IsEmpty())
        {
            // 뜨지 못한 스냅샷이다. 되살릴 수 없는 것은 지우지 않는다.
            //
            // 반쪽도 거절한다. 캔버스는 나무를 통째로 지우는데 스냅샷에 자식이
            // 빠져 있으면 되돌려도 그 자식은 안 돌아온다 - 지우기가 성공했다고
            // 말한 뒤에 조용히 잃는 것이다.
            return false;
        }
        return DestroyTracked();
    }

    void DeleteObjectCommand::Undo()
    {
        Restore();
    }

    void DeleteObjectCommand::Redo()
    {
        DestroyTracked();
    }
}
