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
        m_captured = m_tree.Capture(registry, *object);
    }

    const char* DeleteObjectCommand::GetName() const
    {
        return "Delete Object";
    }

    bool DeleteObjectCommand::Execute()
    {
        if (false == m_captured || m_tree.objects.IsEmpty())
        {
            // 뜨지 못한 스냅샷이다. 되살릴 수 없는 것은 지우지 않는다.
            //
            // 반쪽도 거절한다. 캔버스는 나무를 통째로 지우는데 스냅샷에 자식이
            // 빠져 있으면 되돌려도 그 자식은 안 돌아온다 - 지우기가 성공했다고
            // 말한 뒤에 조용히 잃는 것이다.
            return false;
        }
        return m_tree.DestroyRoot(*m_canvas, *m_registry);
    }

    void DeleteObjectCommand::Undo()
    {
        GameObject* outerParent = m_parentId != InvalidEditorObjectId
            ? m_registry->Resolve(m_parentId)
            : nullptr;
        m_tree.Restore(*m_canvas, *m_registry, outerParent, true);
    }

    void DeleteObjectCommand::Redo()
    {
        m_tree.DestroyRoot(*m_canvas, *m_registry);
    }

    // ── PasteObjectsCommand ──────────────────────────────────────────────────

    PasteObjectsCommand::PasteObjectsCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        const Array<ObjectTreeSnapshot>& trees,
        EditorObjectId parentId)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_parentId(parentId)
    {
        for (std::size_t index = 0; index < trees.Size(); ++index)
        {
            m_trees.Add(trees[index]);
        }
    }

    const char* PasteObjectsCommand::GetName() const
    {
        return "Paste Objects";
    }

    bool PasteObjectsCommand::Paste()
    {
        if (m_trees.IsEmpty())
        {
            return false;
        }
        // 부모가 지워졌으면 뿌리에 붙이지 않고 거절한다. 사용자가 고른 자리가 아니다.
        GameObject* parent = nullptr;
        if (m_parentId != InvalidEditorObjectId)
        {
            parent = m_registry->Resolve(m_parentId);
            if (parent == nullptr)
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            if (false == m_trees[index].Restore(*m_canvas, *m_registry, parent, m_pasted))
            {
                // 반쯤 붙은 것은 도로 지운다. 반쪽을 성공이라 두지 않는다.
                for (std::size_t back = 0; back <= index; ++back)
                {
                    m_trees[back].DestroyRoot(*m_canvas, *m_registry);
                }
                return false;
            }
        }
        m_pasted = true;
        return true;
    }

    void PasteObjectsCommand::DestroyPasted()
    {
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            m_trees[index].DestroyRoot(*m_canvas, *m_registry);
        }
    }

    bool PasteObjectsCommand::Execute()
    {
        return Paste();
    }

    void PasteObjectsCommand::Undo()
    {
        DestroyPasted();
    }

    void PasteObjectsCommand::Redo()
    {
        Paste();
    }

    Array<EditorObjectId> PasteObjectsCommand::GetPastedRootIds() const
    {
        Array<EditorObjectId> ids;
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            ids.Add(m_trees[index].GetRootId());
        }
        return ids;
    }

    RenameObjectCommand::RenameObjectCommand(EditorObjectRegistry& registry,
        EditorObjectId objectId, const char* name)
        : m_registry(&registry)
        , m_objectId(objectId)
        , m_after(name != nullptr ? name : "")
    {
        GameObject* object = registry.Resolve(objectId);
        if (object == nullptr)
        {
            return;
        }
        const char* tag = object->GetTag();
        m_before = tag != nullptr ? tag : "";
        m_captured = true;
    }

    const char* RenameObjectCommand::GetName() const
    {
        return "Rename Object";
    }

    void RenameObjectCommand::Apply(const String& name)
    {
        if (GameObject* object = m_registry->Resolve(m_objectId))
        {
            object->SetTag(name.c_str());
        }
    }

    bool RenameObjectCommand::Execute()
    {
        if (false == m_captured || m_before == m_after)
        {
            return false;
        }
        Apply(m_after);
        return true;
    }

    void RenameObjectCommand::Undo()
    {
        Apply(m_before);
    }

    void RenameObjectCommand::Redo()
    {
        Apply(m_after);
    }

    bool RenameObjectCommand::CanMerge(const EditorCommand& newer) const
    {
        // **같은 오브젝트를 잇달아 고치는 중일 때만 합친다.** 다른 오브젝트로 옮겨 갔는데
        // 합치면 그 이름이 되돌리기에서 사라진다.
        const auto* other = dynamic_cast<const RenameObjectCommand*>(&newer);
        return other != nullptr && other->m_objectId == m_objectId;
    }

    bool RenameObjectCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        // 처음 이름은 이쪽 것을 지킨다 - 친 글자 전체를 한 번에 되돌려야 한다.
        m_after = static_cast<const RenameObjectCommand&>(newer).m_after;
        return true;
    }

    SetObjectActiveCommand::SetObjectActiveCommand(EditorObjectRegistry& registry,
        const Array<EditorObjectId>& objects, bool active)
        : m_registry(&registry)
        , m_after(active)
    {
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            GameObject* object = registry.Resolve(objects[index]);
            if (object == nullptr)
            {
                continue;
            }
            // **되살릴 값을 먼저 뜬다**(§11.5). 못 뜬 것은 목록에 넣지 않는다.
            m_objects.Add(objects[index]);
            m_before.Add(object->IsActiveSelf() ? std::uint8_t{1} : std::uint8_t{0});
        }
    }

    const char* SetObjectActiveCommand::GetName() const
    {
        return "Set Active";
    }

    void SetObjectActiveCommand::Apply(bool active)
    {
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                object->SetActive(active);
            }
        }
    }

    bool SetObjectActiveCommand::Execute()
    {
        if (m_objects.IsEmpty())
        {
            return false;
        }
        // 이미 다 그 값이면 바뀌는 것이 없다. 빈 칸을 쌓아 두면 되돌리기가 한 번 헛돈다.
        bool changes = false;
        for (std::size_t index = 0; index < m_before.Size(); ++index)
        {
            if ((m_before[index] != 0) != m_after)
            {
                changes = true;
            }
        }
        if (false == changes)
        {
            return false;
        }
        Apply(m_after);
        return true;
    }

    void SetObjectActiveCommand::Undo()
    {
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                object->SetActive(m_before[index] != 0);
            }
        }
    }

    void SetObjectActiveCommand::Redo()
    {
        Apply(m_after);
    }

    SetComponentEnabledCommand::SetComponentEnabledCommand(EditorObjectRegistry& registry,
        const ComponentAddress& address, bool enabled)
        : m_registry(&registry)
        , m_address(address)
        , m_after(enabled)
    {
        ComponentBase* component = ResolveComponent(registry, address);
        if (component == nullptr)
        {
            return;
        }
        m_before = component->IsEnabled();
        m_captured = true;
    }

    const char* SetComponentEnabledCommand::GetName() const
    {
        return "Set Component Enabled";
    }

    void SetComponentEnabledCommand::Apply(bool enabled)
    {
        if (ComponentBase* component = ResolveComponent(*m_registry, m_address))
        {
            component->SetEnabled(enabled);
        }
    }

    bool SetComponentEnabledCommand::Execute()
    {
        if (false == m_captured || m_before == m_after)
        {
            return false;
        }
        Apply(m_after);
        return true;
    }

    void SetComponentEnabledCommand::Undo()
    {
        Apply(m_before);
    }

    void SetComponentEnabledCommand::Redo()
    {
        Apply(m_after);
    }
}
