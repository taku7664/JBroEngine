#include <JBro/Runtime/GameObjectHandle.h>

#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObject.h>

#include <cstdio>

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

    bool Handle::GameObject::IsValid() const
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

    void Handle::GameObject::SetActive(bool active)
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("SetActive", m_instanceId);
            return;
        }
        object->SetActive(active);
    }

    bool Handle::GameObject::IsActive() const
    {
        Object::GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("IsActive", m_instanceId);
            return false;
        }
        return object->IsActiveInHierarchy();
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
