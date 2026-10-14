#include <JBro/Canvas/CanvasObjectSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Runtime/GameScriptBase.h>

namespace JBro
{
    namespace
    {
        Bool IsFindable(const Object::GameObject& object)
        {
            return false == object.IsDestroyRequested();
        }

        // 깊이 우선. `visit` 가 참이면 멈추고 참을 돌려준다.
        template<typename Visit>
        Bool WalkTree(Object::GameObject& object, Visit& visit)
        {
            if (false == IsFindable(object))
            {
                return false;
            }
            if (visit(object))
            {
                return true;
            }
            for (const SafePtr<Object::GameObject>& child : object.GetChildren())
            {
                if (Object::GameObject* value = child.TryGet())
                {
                    if (WalkTree(*value, visit))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        Bool Matches(const Object::GameObject& object, NameId name, Bool byTag)
        {
            return byTag ? object.GetTagId() == name : object.GetNameId() == name;
        }
    }

    void CanvasObjectSystem::Bind(Canvas* canvas, const char* defaultComponent)
    {
        m_canvas = canvas;
        m_defaultComponent = defaultComponent != nullptr ? defaultComponent : "";
        m_roots.Clear();
    }

    template<typename Visit>
    void CanvasObjectSystem::Walk(Visit&& visit)
    {
        if (m_canvas == nullptr)
        {
            return;
        }
        // 뿌리 목록은 멤버로 들고 다시 쓴다 - 찾기마다 배열을 새로 잡지 않는다.
        m_canvas->GetRootObjects(m_roots);
        for (std::size_t layerIndex = 0; layerIndex < m_canvas->GetLayerCount(); ++layerIndex)
        {
            const Layer* layer = m_canvas->GetLayerAt(layerIndex);
            if (layer == nullptr)
            {
                continue;
            }
            for (Object::GameObject* root : m_roots)
            {
                if (root->GetLayerId() != layer->GetId())
                {
                    continue;
                }
                if (WalkTree(*root, visit))
                {
                    return;
                }
            }
        }
    }

    Handle::GameObject CanvasObjectSystem::Create(const char* name, const Handle::GameObject& parent, UInt32 layer)
    {
        if (m_canvas == nullptr)
        {
            return {};
        }
        Object::GameObject* parentObject = nullptr;
        if (parent.GetInstanceId() != InvalidInstanceId)
        {
            parentObject = Internal::GameObjectHandleAccess::Resolve(parent);
            if (parentObject == nullptr || parentObject->GetCanvas() != m_canvas)
            {
                // 무효한 부모를 뿌리로 읽으면 뜻하지 않은 곳에 만든다. 만들지 않는다.
                return {};
            }
        }
        Object::GameObject* object = m_canvas->CreateObject(name);
        if (object == nullptr)
        {
            return {};
        }
        // 레이어: 준 것 → 부모의 것 → 캔버스가 준 기본 레이어(에디터의 새 오브젝트와 같다).
        if (layer != InvalidLayer && m_canvas->FindLayer(layer) != nullptr)
        {
            m_canvas->SetObjectLayer(object, layer);
        }
        else if (parentObject != nullptr)
        {
            m_canvas->SetObjectLayer(object, parentObject->GetLayerId());
        }
        if (parentObject != nullptr)
        {
            object->SetParent(parentObject);
        }
        if (false == m_defaultComponent.empty())
        {
            if (const ComponentTypeInfo* info = ComponentRegistry::Get().Find(m_defaultComponent.c_str()))
            {
                if (info->Attach != nullptr)
                {
                    info->Attach(*m_canvas, object, InvalidInstanceId);
                }
            }
        }
        return object->GetScriptHandle();
    }

    Handle::GameObject CanvasObjectSystem::Find(NameId name, Bool byTag)
    {
        Handle::GameObject found;
        auto visit = [&found, name, byTag](Object::GameObject& object)
        {
            if (Matches(object, name, byTag))
            {
                found = object.GetScriptHandle();
                return true;
            }
            return false;
        };
        Walk(visit);
        return found;
    }

    void CanvasObjectSystem::FindAll(NameId name, Bool byTag, Array<Handle::GameObject>& results)
    {
        results.Clear();
        auto visit = [&results, name, byTag](Object::GameObject& object)
        {
            if (Matches(object, name, byTag))
            {
                results.Add(object.GetScriptHandle());
            }
            return false;
        };
        Walk(visit);
    }

    Handle::GameObject CanvasObjectSystem::FindRoot(UInt32 layer, NameId name)
    {
        if (m_canvas == nullptr)
        {
            return {};
        }
        m_canvas->GetRootObjects(m_roots);
        for (Object::GameObject* root : m_roots)
        {
            if (root->GetLayerId() == layer && IsFindable(*root) && root->GetNameId() == name)
            {
                return root->GetScriptHandle();
            }
        }
        return {};
    }

    UInt32 CanvasObjectSystem::FindLayer(NameId name)
    {
        if (m_canvas == nullptr)
        {
            return InvalidLayer;
        }
        for (std::size_t index = 0; index < m_canvas->GetLayerCount(); ++index)
        {
            const Layer* layer = m_canvas->GetLayerAt(index);
            if (layer != nullptr && MakeNameId(layer->GetName()) == name)
            {
                return layer->GetId();
            }
        }
        return InvalidLayer;
    }

    Bool CanvasObjectSystem::IsLayerAlive(UInt32 layer)
    {
        return m_canvas != nullptr && m_canvas->FindLayer(layer) != nullptr;
    }

    const char* CanvasObjectSystem::GetLayerName(UInt32 layer)
    {
        const Layer* found = m_canvas != nullptr ? m_canvas->FindLayer(layer) : nullptr;
        return found != nullptr ? found->GetName() : "";
    }

    InstanceRef CanvasObjectSystem::AddComponent(const Handle::GameObject& object, ComponentTypeId type)
    {
        Object::GameObject* owner = Internal::GameObjectHandleAccess::Resolve(object);
        if (m_canvas == nullptr || owner == nullptr || owner->GetCanvas() != m_canvas)
        {
            return {};
        }
        // 레지스트리는 타입 이름의 id 로 찾는다 - 컴포넌트의 타입 id 가 바로 그것이다.
        const ComponentTypeInfo* info = ComponentRegistry::Get().Find(static_cast<NameId>(type));
        if (info == nullptr || info->Attach == nullptr)
        {
            return {};
        }
        ComponentBase* component = info->Attach(*m_canvas, owner, InvalidInstanceId);
        if (component == nullptr)
        {
            return {};
        }
        InstanceRef reference;
        reference.ObjectId = owner->GetInstanceId();
        reference.ComponentId = component->GetInstanceId();
        reference.Cached = component->GetHandle();
        return reference;
    }

    InstanceRef CanvasObjectSystem::AddScript(const Handle::GameObject& object, const char* typeName)
    {
        Object::GameObject* owner = Internal::GameObjectHandleAccess::Resolve(object);
        if (m_canvas == nullptr || owner == nullptr || owner->GetCanvas() != m_canvas || typeName == nullptr)
        {
            return {};
        }
        GameScriptBase* script = m_canvas->AttachScript(owner, typeName);
        if (script == nullptr)
        {
            return {};
        }
        InstanceRef reference;
        reference.ObjectId = owner->GetInstanceId();
        reference.ComponentId = script->GetInstanceId();
        reference.Cached = script->GetHandle();
        return reference;
    }
}
