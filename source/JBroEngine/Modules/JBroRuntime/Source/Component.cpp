#include <JBro/Runtime/Component.h>

#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    InstanceId ComponentBase::GetInstanceId() const
    {
        return m_instanceId;
    }

    InstanceHandle ComponentBase::GetHandle() const
    {
        return m_handle;
    }

    ComponentTypeId ComponentBase::GetCachedTypeId() const
    {
        return m_typeId;
    }

    void ComponentBase::CacheTypeId()
    {
        m_typeId = GetTypeId();
    }

    GameObjectHandle ComponentBase::GetOwner() const
    {
        GameObject* owner = m_owner.TryGet();
        return owner == nullptr ? GameObjectHandle() : owner->GetScriptHandle();
    }

    GameObject* ComponentBase::GetOwnerObject() const
    {
        return m_owner.TryGet();
    }

    bool ComponentBase::IsActiveComponent() const
    {
        GameObject* owner = m_owner.TryGet();
        return m_enabled && owner != nullptr && owner->IsActiveInHierarchy();
    }

    void ComponentBase::OnAttached()
    {
    }

    void ComponentBase::OnDetached()
    {
    }

    void ComponentBase::OnEnabled()
    {
    }

    void ComponentBase::OnDisabled()
    {
    }

    bool ComponentBase::IsEnabled() const
    {
        return m_enabled;
    }

    void ComponentBase::SetEnabled(bool enabled)
    {
        if (m_enabled == enabled)
        {
            return;
        }
        m_enabled = enabled;
        if (enabled)
        {
            OnEnabled();
            return;
        }
        OnDisabled();
    }

    void ComponentBase::SetOwner(GameObject* owner)
    {
        if (owner == nullptr)
        {
            m_owner.Reset();
            return;
        }
        m_owner = owner->SafeFromThis();
    }

    void ComponentBase::SetInstanceIdentity(
        InstanceId instanceId,
        InstanceHandle handle)
    {
        m_instanceId = instanceId;
        m_handle = handle;
    }
}
