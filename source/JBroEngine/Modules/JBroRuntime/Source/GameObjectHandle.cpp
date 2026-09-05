#include <JBro/Runtime/GameObjectHandle.h>

#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObject.h>

#include <cstdio>

namespace JBro
{
    GameObjectHandle::GameObjectHandle(const GameObject* object)
    {
        if (object != nullptr)
        {
            m_cached = object->GetHandle();
            m_instanceId = object->GetInstanceId();
        }
    }

    bool GameObjectHandle::IsValid() const
    {
        return Resolve() != nullptr;
    }

    GameObjectHandle::operator bool() const
    {
        return IsValid();
    }

    void GameObjectHandle::Destroy()
    {
        GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("Destroy", m_instanceId);
            return;
        }
        object->RequestDestroy();
        m_cached = {};
    }

    void GameObjectHandle::SetActive(bool active)
    {
        GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("SetActive", m_instanceId);
            return;
        }
        object->SetActive(active);
    }

    bool GameObjectHandle::IsActive() const
    {
        GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("IsActive", m_instanceId);
            return false;
        }
        return object->IsActiveInHierarchy();
    }

    InstanceId GameObjectHandle::GetInstanceId() const
    {
        return m_instanceId;
    }

    InstanceRef GameObjectHandle::FindComponentReference(ComponentTypeId typeId) const
    {
        GameObject* object = Resolve();
        if (object == nullptr)
        {
            ReportInvalidAccess("GetComponent", m_instanceId);
            return {};
        }
        return object->FindComponentReference(typeId);
    }

    void GameObjectHandle::ReportInvalidAccess(
        const char* operation,
        InstanceId instanceId)
    {
        std::fprintf(
            stderr,
            "JBro warning: GameObjectHandle::%s on invalid handle (id=%llu)\n",
            operation,
            static_cast<unsigned long long>(instanceId));
    }

    GameObject* GameObjectHandle::Resolve() const
    {
        if (m_cached.IsSet())
        {
            void* cached = Internal::ResolveInstanceByHandle(
                m_cached,
                RefCategory::Object);
            if (cached != nullptr)
            {
                return static_cast<GameObject*>(cached);
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
        return static_cast<GameObject*>(resolved.Pointer);
    }
}
