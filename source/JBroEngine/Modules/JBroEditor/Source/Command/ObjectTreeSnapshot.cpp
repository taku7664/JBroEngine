#include <JBro/Editor/Command/ObjectTreeSnapshot.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/NameTable.h>

#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool ObjectTreeSnapshot::Capture(const Canvas& canvas, EditorObjectRegistry& registry, Object::GameObject& root)
    {
        objects.Clear();
        return CaptureInto(canvas, registry, root, -1);
    }

    Bool ObjectTreeSnapshot::CaptureInto(
        const Canvas& canvas, EditorObjectRegistry& registry, Object::GameObject& object, Int64 parentIndex)
    {
        ObjectSnapshotEntry entry;
        entry.id = registry.Track(&object);
        const char* name = object.GetName();
        entry.name = name != nullptr ? name : "";
        const char* tag = object.GetTagId() != InvalidNameId ? object.GetTag() : nullptr;
        entry.tag = tag != nullptr ? tag : "";
        entry.active = object.IsActiveSelf();
        entry.flags = object.GetFlags();
        entry.layer = object.GetLayerId();
        entry.parentIndex = parentIndex;
        entry.instanceId = object.GetInstanceId();
        entry.sourceInstanceId = entry.instanceId;

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
        for (const ScriptSlot& slot : object.GetScripts())
        {
            GameScriptBase* script = slot.reference.TryGet();
            if (script == nullptr)
            {
                continue;
            }
            ComponentSnapshot captured;
            if (false == CaptureAttached(script, captured))
            {
                return false;
            }
            entry.scripts.Add(std::move(captured));
        }
        if (const Array<UnresolvedScript>* kept = canvas.FindUnresolvedScripts(&object))
        {
            entry.unresolved = *kept;
        }

        const Int64 self = static_cast<JBro::Int64>(objects.Size());
        objects.Add(std::move(entry));

        const Array<SafePtr<Object::GameObject>>& children = object.GetChildren();
        for (std::size_t index = 0; index < children.Size(); ++index)
        {
            if (Object::GameObject* child = children[index].TryGet())
            {
                if (false == CaptureInto(canvas, registry, *child, self))
                {
                    return false;
                }
            }
        }
        return true;
    }

    Bool ObjectTreeSnapshot::Restore(
        Canvas& canvas, EditorObjectRegistry& registry, Object::GameObject* outerParent, Bool rebind)
    {
        // 만든 것을 순서대로 들고 있는다. 부모는 늘 먼저 나오므로 앞에서부터
        // 만들면 붙일 자리가 이미 있다.
        Array<Object::GameObject*> created;
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            ObjectSnapshotEntry& entry = objects[index];
            // 되살리기는 옛 번호로, 붙여넣기의 첫 실행은 새 번호로 만든다. 다시 하기는 첫 실행이 받은 번호를 다시 쓴다.
            Object::GameObject* object = canvas.CreateObject(entry.name.c_str(), rebind ? entry.instanceId : InvalidInstanceId);
            if (object == nullptr)
            {
                return false;
            }
            entry.instanceId = object->GetInstanceId();
            Object::GameObject* parent = entry.parentIndex < 0
                ? outerParent
                : created[static_cast<std::size_t>(entry.parentIndex)];
            if (parent != nullptr)
            {
                object->SetParent(parent);
            }
            object->SetActive(entry.active);
            object->SetFlags(entry.flags);
            object->SetTag(entry.tag.c_str());
            // **떠 둔 레이어로 보낸다**(D-168). 그 레이어가 그 사이에 사라졌으면 캔버스가
            // 새로 만든 것에 준 레이어(기본 레이어)로 둔다 - 없는 번호를 들고 있으면
            // 그 오브젝트는 어느 칸에도 나오지 않는다.
            if (entry.layer != InvalidLayerId && canvas.FindLayer(entry.layer) != nullptr)
            {
                canvas.SetObjectLayer(object, entry.layer);
            }
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

            // 컴포넌트 다음에 스크립트다. 어느 목록인지는 떠 둔 것이 안다(D-302).
            for (Array<ComponentSnapshot>* list : { &entry.components, &entry.scripts })
            {
                for (ComponentSnapshot& captured : *list)
                {
                    const char* typeName = NameTable::Get().Resolve(captured.typeId);
                    if (typeName == nullptr)
                    {
                        return false;
                    }
                    // 되살리기는 옛 번호로 붙인다(오브젝트와 같다). 붙여넣기는 새 번호다.
                    const AttachedRef attached = AttachByName(canvas, *object, MakeNameId(typeName),
                        rebind ? captured.instanceId : InvalidInstanceId);
                    if (false == attached.IsValid() || false == ApplyAttached(attached, captured))
                    {
                        return false;
                    }
                    // 받은 번호를 적어 두면 다음 다시 하기가 같은 번호로 되살린다.
                    captured.instanceId = attached.GetInstanceId();
                }
            }
            for (std::size_t u = 0; u < entry.unresolved.Size(); ++u)
            {
                if (false == canvas.AddUnresolvedScript(object, entry.unresolved[u]))
                {
                    return false;
                }
            }
        }
        RetargetReferences(created);
        return true;
    }

    void ObjectTreeSnapshot::RetargetReferences(const Array<Object::GameObject*>& created) const
    {
        // **나무 안의 참조는 나무 안의 새 오브젝트로 옮긴다**(D-233). 붙여넣은 조인트가 원본의 상대를 붙잡지 않고 함께 붙여넣은
        // 상대를 잡는다. 나무 밖을 가리키는 참조는 그대로 둔다. 맨 위 필드만 본다 - 참조 필드를 가진 컴포넌트가 그렇게 선언한다.
        Bool moved = false;
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            moved = moved || objects[index].sourceInstanceId != objects[index].instanceId;
        }
        if (false == moved)
        {
            return;
        }
        const NameId handleType = NameTable::Get().Intern("JBro.Handle.GameObject");
        const NameId componentHandleType = NameTable::Get().Intern("JBro.ComponentHandle");
        Array<AttachedRef> attached;
        for (Object::GameObject* object : created)
        {
            // 컴포넌트와 스크립트 모두의 참조 필드를 옮긴다(D-302).
            attached.Clear();
            for (const ComponentSlot& slot : object->GetComponents())
            {
                attached.Add(slot.reference.TryGet());
            }
            for (const ScriptSlot& slot : object->GetScripts())
            {
                attached.Add(slot.reference.TryGet());
            }
            for (const AttachedRef& target : attached)
            {
                void* instance = target.GetInstance();
                const PropertyTable* table = instance != nullptr ? PropertyRegistry::Lookup(target.GetTypeId()) : nullptr;
                if (table == nullptr)
                {
                    continue;
                }
                for (UInt32 p = 0; p < table->count; ++p)
                {
                    const PropertyInfo& property = table->properties[p];
                    if (property.type == nullptr || property.Address == nullptr)
                    {
                        continue;
                    }
                    if (property.type->typeName == componentHandleType)
                    {
                        RetargetComponentHandle(
                            *static_cast<Internal::ComponentHandle*>(property.Address(instance)), created);
                        continue;
                    }
                    if (property.type->typeName != handleType)
                    {
                        continue;
                    }
                    Handle::GameObject& handle = *static_cast<Handle::GameObject*>(property.Address(instance));
                    for (const ObjectSnapshotEntry& entry : objects)
                    {
                        if (entry.sourceInstanceId != InvalidInstanceId && handle.GetInstanceId() == entry.sourceInstanceId)
                        {
                            handle = Internal::GameObjectHandleAccess::FromId(entry.instanceId);
                            break;
                        }
                    }
                }
            }
        }
    }

    void ObjectTreeSnapshot::RetargetComponentHandle(
        Internal::ComponentHandle& handle, const Array<Object::GameObject*>& created) const
    {
        // 나무 안의 컴포넌트를 가리키면 새 오브젝트의 같은 자리 컴포넌트로 옮긴다(D-302). 컴포넌트는 뜬 차례대로 다시 붙으므로
        // 뜬 목록의 몇 번째가 새 오브젝트 컴포넌트 목록의 몇 번째다.
        const InstanceRef& reference = Internal::ComponentHandleAccess::GetReference(handle);
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            const ObjectSnapshotEntry& entry = objects[index];
            if (entry.sourceInstanceId == InvalidInstanceId || reference.ObjectId != entry.sourceInstanceId)
            {
                continue;
            }
            const Array<ComponentSlot>& slots = created[index]->GetComponents();
            for (std::size_t c = 0; c < entry.components.Size() && c < slots.Size(); ++c)
            {
                ComponentBase* component = slots[c].reference.TryGet();
                if (entry.components[c].sourceInstanceId != reference.ComponentId || component == nullptr
                    || slots[c].typeId != entry.components[c].typeId)
                {
                    continue;
                }
                InstanceRef moved;
                moved.ObjectId = created[index]->GetInstanceId();
                moved.ComponentId = component->GetInstanceId();
                moved.Cached = component->GetHandle();
                Internal::ComponentHandleAccess::SetReference(handle, moved);
                return;
            }
            return;
        }
    }

    Bool ObjectTreeSnapshot::DestroyRoot(Canvas& canvas, EditorObjectRegistry& registry) const
    {
        if (objects.IsEmpty())
        {
            return false;
        }
        Object::GameObject* object = registry.Resolve(objects[0].id);
        if (object == nullptr)
        {
            return false;
        }
        // 자식은 캔버스가 함께 지운다. 스냅샷에는 그 자식들도 들어 있으므로
        // 되살릴 때 나무가 통째로 돌아온다.
        const Bool destroyed = canvas.DestroyObject(object);
        canvas.FlushPendingDestroy();
        return destroyed;
    }
}
