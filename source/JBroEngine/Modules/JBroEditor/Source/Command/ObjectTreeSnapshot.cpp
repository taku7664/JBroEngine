#include <JBro/Editor/Command/ObjectTreeSnapshot.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <utility>

namespace JBro
{
    bool ObjectTreeSnapshot::Capture(EditorObjectRegistry& registry, GameObject& root)
    {
        objects.Clear();
        return CaptureInto(registry, root, -1);
    }

    bool ObjectTreeSnapshot::CaptureInto(
        EditorObjectRegistry& registry, GameObject& object, std::int64_t parentIndex)
    {
        ObjectSnapshotEntry entry;
        entry.id = registry.Track(&object);
        const char* name = object.GetTag();
        entry.name = name != nullptr ? name : "";
        entry.active = object.IsActiveSelf();
        entry.flags = object.GetFlags();
        entry.parentIndex = parentIndex;

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
                // 뜨기를 거절한다 - 조용히 잃는 것보다 낫다.
                return false;
            }
            entry.components.Add(std::move(captured));
        }

        const std::int64_t self = static_cast<std::int64_t>(objects.Size());
        objects.Add(std::move(entry));

        const Array<SafePtr<GameObject>>& children = object.GetChildren();
        for (std::size_t index = 0; index < children.Size(); ++index)
        {
            if (GameObject* child = children[index].TryGet())
            {
                if (false == CaptureInto(registry, *child, self))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool ObjectTreeSnapshot::Restore(
        Canvas& canvas, EditorObjectRegistry& registry, GameObject* outerParent, bool rebind)
    {
        // 만든 것을 순서대로 들고 있는다. 부모는 늘 먼저 나오므로 앞에서부터
        // 만들면 붙일 자리가 이미 있다.
        Array<GameObject*> created;
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            ObjectSnapshotEntry& entry = objects[index];
            GameObject* object = canvas.CreateObject(entry.name.c_str());
            if (object == nullptr)
            {
                return false;
            }
            GameObject* parent = entry.parentIndex < 0
                ? outerParent
                : created[static_cast<std::size_t>(entry.parentIndex)];
            if (parent != nullptr)
            {
                object->SetParent(parent);
            }
            object->SetActive(entry.active);
            object->SetFlags(entry.flags);
            if (rebind)
            {
                // **옛 번호에 다시 건다.** 이 오브젝트를 가리키던 커맨드들이 계속 찾아야 한다.
                registry.Rebind(entry.id, object);
            }
            else
            {
                // 새 오브젝트다. 번호를 받아 적어 두면 다음 다시 하기가 같은 번호로 되살린다.
                entry.id = registry.Track(object);
            }
            created.Add(object);

            for (std::size_t c = 0; c < entry.components.Size(); ++c)
            {
                const ComponentSnapshot& captured = entry.components[c];
                const char* typeName = NameTable::Get().Resolve(captured.typeId);
                const ComponentTypeInfo* info = typeName != nullptr
                    ? ComponentRegistry::Get().Find(typeName)
                    : nullptr;
                if (info == nullptr || info->Attach == nullptr)
                {
                    return false;
                }
                ComponentBase* component = info->Attach(canvas, object);
                if (component == nullptr || false == ApplyComponent(*component, captured))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool ObjectTreeSnapshot::DestroyRoot(Canvas& canvas, EditorObjectRegistry& registry) const
    {
        if (objects.IsEmpty())
        {
            return false;
        }
        GameObject* object = registry.Resolve(objects[0].id);
        if (object == nullptr)
        {
            return false;
        }
        // 자식은 캔버스가 함께 지운다. 스냅샷에는 그 자식들도 들어 있으므로
        // 되살릴 때 나무가 통째로 돌아온다.
        const bool destroyed = canvas.DestroyObject(object);
        canvas.FlushPendingDestroy();
        return destroyed;
    }
}
