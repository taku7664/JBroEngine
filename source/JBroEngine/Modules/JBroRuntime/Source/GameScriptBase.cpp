#include <JBro/Runtime/GameScriptBase.h>

#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    InstanceId GameScriptBase::GetInstanceId() const
    {
        return m_instanceId;
    }

    InstanceHandle GameScriptBase::GetHandle() const
    {
        return m_handle;
    }

    ComponentTypeId GameScriptBase::GetCachedTypeId() const
    {
        return m_typeId;
    }

    void GameScriptBase::CacheTypeId()
    {
        m_typeId = GetTypeId();
    }

    Handle::GameObject GameScriptBase::GetGameObject() const
    {
        Object::GameObject* owner = m_owner.TryGet();
        return owner == nullptr ? Handle::GameObject() : owner->GetScriptHandle();
    }

    Object::GameObject* GameScriptBase::GetOwnerObject() const
    {
        return m_owner.TryGet();
    }

    bool GameScriptBase::IsActiveScript() const
    {
        Object::GameObject* owner = m_owner.TryGet();
        return m_enabled && owner != nullptr && owner->IsActiveInHierarchy();
    }

    bool GameScriptBase::IsEnabled() const
    {
        return m_enabled;
    }

    void GameScriptBase::SetEnabled(bool enabled)
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

    void GameScriptBase::SetOwner(Object::GameObject* owner)
    {
        if (owner == nullptr)
        {
            m_owner.Reset();
            return;
        }
        m_owner = owner->SafeFromThis();
    }

    void GameScriptBase::SetInstanceIdentity(InstanceId instanceId, InstanceHandle handle)
    {
        m_instanceId = instanceId;
        m_handle = handle;
    }

    void GameScriptBase::OnAttached()
    {
    }

    void GameScriptBase::OnDetached()
    {
    }

    void GameScriptBase::OnEnabled()
    {
    }

    void GameScriptBase::OnDisabled()
    {
    }

    void GameScriptBase::OnCreate()
    {
    }

    void GameScriptBase::OnStart()
    {
    }

    void GameScriptBase::OnUpdate()
    {
    }

    void GameScriptBase::OnFixedUpdate()
    {
    }

    void GameScriptBase::OnDestroy()
    {
    }
}
