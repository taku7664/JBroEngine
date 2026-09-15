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
}
