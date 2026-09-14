#include <JBro/Editor/EditorObjectRegistry.h>

#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    EditorObjectId EditorObjectRegistry::Track(GameObject* object)
    {
        if (object == nullptr)
        {
            return InvalidEditorObjectId;
        }
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].object.TryGet() == object)
            {
                return m_entries[index].id;
            }
        }

        Entry entry;
        entry.id = m_nextId++;
        entry.object = object->SafeFromThis();
        m_entries.Add(entry);
        return entry.id;
    }

    GameObject* EditorObjectRegistry::Resolve(EditorObjectId id) const
    {
        if (id == InvalidEditorObjectId)
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].id == id)
            {
                return m_entries[index].object.TryGet();
            }
        }
        return nullptr;
    }

    bool EditorObjectRegistry::Rebind(EditorObjectId id, GameObject* object)
    {
        if (id == InvalidEditorObjectId || object == nullptr)
        {
            return false;
        }
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].id == id)
            {
                m_entries[index].object = object->SafeFromThis();
                return true;
            }
        }
        // 모르는 번호다. 되살리는 쪽이 번호를 지어내면 안 된다 - 그 번호를 들고
        // 있던 커맨드가 엉뚱한 오브젝트를 찾게 된다.
        return false;
    }

    void EditorObjectRegistry::Clear()
    {
        m_entries.Clear();
        // 번호는 이어서 센다. 지운 뒤에 남아 있는 커맨드가 옛 번호로 새 오브젝트를
        // 찾는 일이 없도록 한다.
        ;
    }

    std::size_t EditorObjectRegistry::GetCount() const
    {
        return m_entries.Size();
    }
}
