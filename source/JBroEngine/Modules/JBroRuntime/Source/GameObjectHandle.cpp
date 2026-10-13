#include <JBro/Runtime/GameObjectHandle.h>

#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/IObjectSystem.h>
#include <JBro/Runtime/SystemContext.h>

#include <cstdio>
#include <JBro/Types/Bool.h>

namespace JBro
{
    Handle::GameObject::GameObject(const Object::GameObject* object)
    {
        if (object != nullptr)
        {
            m_cached = object->GetHandle();
            m_instanceId = object->GetInstanceId();
        }
    }

    Bool Handle::GameObject::IsValid() const
    {
        return Resolve() != nullptr;
    }

    Handle::GameObject::operator bool() const
    {
        return IsValid();
    }

    void Handle::GameObject::Destroy()
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("Destroy", m_instanceId);
            return;
        }
        object->RequestDestroy();
        m_cached = {};
    }

    void Handle::GameObject::SetActive(Bool active)
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("SetActive", m_instanceId);
            return;
        }
        object->SetActive(active);
    }

    Bool Handle::GameObject::IsActive() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("IsActive", m_instanceId);
            return false;
        }
        return object->IsActiveInHierarchy();
    }

    const char* Handle::GameObject::GetTag() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetTag", m_instanceId);
            return "";
        }
        const char* tag = object->GetTag();
        return tag != nullptr ? tag : "";
    }

    void Handle::GameObject::SetTag(const char* tag)
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("SetTag", m_instanceId);
            return;
        }
        object->SetTag(tag);
    }

    Bool Handle::GameObject::CompareTag(const char* tag) const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("CompareTag", m_instanceId);
            return false;
        }
        return object->CompareTag(tag);
    }

    const char* Handle::GameObject::GetName() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetName", m_instanceId);
            return "";
        }
        const char* name = object->GetName();
        return name != nullptr ? name : "";
    }

    void Handle::GameObject::SetName(const char* name)
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("SetName", m_instanceId);
            return;
        }
        object->SetName(name);
    }

    Handle::Layer Handle::GameObject::GetLayer() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetLayer", m_instanceId);
            return {};
        }
        // 오브젝트가 드는 것은 캔버스 레이어의 영속 번호다.
        return LayerAccess::FromId(object->GetLayerId());
    }

    Handle::GameObject Handle::GameObject::GetParent() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetParent", m_instanceId);
            return {};
        }
        Object::GameObject* parent = object->GetParent();
        return parent == nullptr ? GameObject() : GameObject(parent);
    }

    void Handle::GameObject::SetParent(GameObject parent)
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("SetParent", m_instanceId);
            return;
        }
        Object::GameObject* parentObject = nullptr;
        if (parent.m_instanceId != InvalidInstanceId)
        {
            parentObject = parent.Resolve();
            if (parentObject == nullptr || parentObject->GetCanvas() != object->GetCanvas())
            {
                // 무효한 부모를 뿌리로 읽으면 사용자가 뜻하지 않은 곳으로 옮긴다. 아무것도 하지 않는다.
                ReportInvalidAccess("SetParent(parent)", parent.m_instanceId);
                return;
            }
        }
        // 자기 자손 밑으로 옮기는 것은 `Object::GameObject::SetParent` 가 거절한다. 레이어는 맞추지 않는다 - 에디터의 계층 옮기기와 같다.
        object->SetParent(parentObject);
    }

    UInt32 Handle::GameObject::GetChildCount() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetChildCount", m_instanceId);
            return 0;
        }
        UInt32 count = 0;
        for (const SafePtr<Object::GameObject>& child : object->GetChildren())
        {
            const Object::GameObject* value = child.TryGet();
            if (value != nullptr && false == value->m_destroying)
            {
                ++count;
            }
        }
        return count;
    }

    Handle::GameObject Handle::GameObject::GetChild(UInt32 index) const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetChild", m_instanceId);
            return {};
        }
        UInt32 seen = 0;
        for (const SafePtr<Object::GameObject>& child : object->GetChildren())
        {
            const Object::GameObject* value = child.TryGet();
            if (value == nullptr || value->m_destroying)
            {
                continue;
            }
            if (seen == index)
            {
                return GameObject(value);
            }
            ++seen;
        }
        return {};
    }

    Handle::GameObject Handle::GameObject::operator[](const char* name) const
    {
        return (*this)[MakeNameId(name)];
    }

    Handle::GameObject Handle::GameObject::operator[](NameId name) const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("operator[]", m_instanceId);
            return {};
        }
        if (name == InvalidNameId)
        {
            return {};
        }
        for (const SafePtr<Object::GameObject>& child : object->GetChildren())
        {
            const Object::GameObject* value = child.TryGet();
            if (value != nullptr && false == value->m_destroying && value->GetNameId() == name)
            {
                return GameObject(value);
            }
        }
        return {};
    }

    const Object::GameObject* Handle::GameObject::FindDescendant(const Object::GameObject& parent, NameId name)
    {
        for (const SafePtr<Object::GameObject>& child : parent.GetChildren())
        {
            const Object::GameObject* value = child.TryGet();
            if (value == nullptr || value->m_destroying)
            {
                continue;
            }
            if (value->GetNameId() == name)
            {
                return value;
            }
            if (const Object::GameObject* found = FindDescendant(*value, name))
            {
                return found;
            }
        }
        return nullptr;
    }

    Handle::GameObject Handle::GameObject::FindInChildren(const char* name) const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("FindInChildren", m_instanceId);
            return {};
        }
        const NameId id = MakeNameId(name);
        if (id == InvalidNameId)
        {
            return {};
        }
        const Object::GameObject* found = FindDescendant(*object, id);
        return found == nullptr ? GameObject() : GameObject(found);
    }

    InstanceRef Handle::GameObject::AddComponentReference(ComponentTypeId typeId)
    {
        if (Resolve() == nullptr)
        {
            ReportInvalidAccess("AddComponent", m_instanceId);
            return {};
        }
        System::IObjectSystem* objects = GetSystemContext().Objects;
        if (objects == nullptr)
        {
            return {};
        }
        return objects->AddComponent(*this, typeId);
    }

    InstanceRef Handle::GameObject::AddScriptReference(const char* typeName)
    {
        if (Resolve() == nullptr)
        {
            ReportInvalidAccess("AddScript", m_instanceId);
            return {};
        }
        System::IObjectSystem* objects = GetSystemContext().Objects;
        if (objects == nullptr)
        {
            return {};
        }
        return objects->AddScript(*this, typeName);
    }

    InstanceId Handle::GameObject::GetInstanceId() const
    {
        return m_instanceId;
    }

    InstanceRef Handle::GameObject::FindComponentReference(ComponentTypeId typeId) const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetComponent", m_instanceId);
            return {};
        }
        return object->FindComponentReference(typeId);
    }

    InstanceRef Handle::GameObject::FindScriptReference(ComponentTypeId typeId) const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetScript", m_instanceId);
            return {};
        }
        return object->FindScriptReference(typeId);
    }

    void Handle::GameObject::ReportInvalidAccess(
        const char* operation,
        InstanceId instanceId)
    {
        std::fprintf(
            stderr,
            "JBro warning: GameObject::%s on invalid handle (id=%llu)\n",
            operation,
            static_cast<unsigned long long>(instanceId));
    }

    Object::GameObject* Handle::GameObject::Resolve() const
    {
        Object::GameObject* object = ResolveLiveOrDestroying();
        // **파괴를 요청한 오브젝트는 이미 없는 것이다**(D-301 (6)). 순회 중에는 안전 지점까지 남아 있지만, 그 사이에 다른 사본이
        // 그것을 찾아 다시 때리면 안 된다.
        if (object == nullptr || object->m_destroying)
        {
            return nullptr;
        }
        return object;
    }

    Object::GameObject* Handle::GameObject::ResolveLiveOrDestroying() const
    {
        if (m_cached.IsSet())
        {
            void* cached = Internal::ResolveInstanceByHandle(
                m_cached,
                RefCategory::Object);
            if (cached != nullptr)
            {
                return static_cast<Object::GameObject*>(cached);
            }
        }

        if (m_instanceId == InvalidInstanceId)
        {
            return nullptr;
        }

        const Internal::ResolvedInstance resolved = Internal::ResolveInstanceById(
            m_instanceId,
            InvalidInstanceId,
            RefCategory::Object);
        if (resolved.Pointer == nullptr)
        {
            return nullptr;
        }
        m_cached = resolved.Handle;
        return static_cast<Object::GameObject*>(resolved.Pointer);
    }
}
