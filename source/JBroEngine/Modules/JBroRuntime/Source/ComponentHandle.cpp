#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ScriptLogContext.h>

#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>

#include <cstdio>

namespace JBro::Internal
{
    Bool ComponentHandle::IsValid() const
    {
        return ResolveSilently() != nullptr;
    }

    ComponentHandle::operator bool() const
    {
        return IsValid();
    }

    Handle::GameObject ComponentHandle::GetGameObject() const
    {
        const ComponentBase* component = ResolveComponent("ComponentHandle", "GetGameObject");
        if (component == nullptr)
        {
            return {};
        }
        return component->GetOwner();
    }

    Bool ComponentHandle::IsEnabled() const
    {
        const ComponentBase* component = ResolveComponent("ComponentHandle", "IsEnabled");
        if (component == nullptr)
        {
            return false;
        }
        return component->IsEnabled();
    }

    void ComponentHandle::SetEnabled(Bool enabled)
    {
        ComponentBase* component = ResolveComponent("ComponentHandle", "SetEnabled");
        if (component == nullptr)
        {
            return;
        }
        component->SetEnabled(enabled);
    }

    Bool ComponentHandle::operator==(const ComponentHandle& other) const
    {
        return m_reference.ObjectId == other.m_reference.ObjectId
            && m_reference.ComponentId == other.m_reference.ComponentId;
    }

    ComponentBase* ComponentHandle::ResolveComponent(const char* typeName, const char* operation) const
    {
        ComponentBase* component = ResolveSilently();
        if (component == nullptr)
        {
            // `Handle::GameObject` 의 무효 접근과 같은 길이다(D-380). 무효한 경우에만 글자를 만든다.
            char key[96];
            char message[224];
            std::snprintf(key, sizeof(key), "handle.%s.%s", typeName, operation);
            std::snprintf(message, sizeof(message), "%s::%s on invalid handle (object=%llu, component=%llu)", typeName, operation,
                static_cast<unsigned long long>(m_reference.ObjectId),
                static_cast<unsigned long long>(m_reference.ComponentId));
            WriteEngineLog(LogLevel::Warning, "script", key, message);
        }
        return component;
    }

    ComponentBase* ComponentHandle::ResolveSilently() const
    {
        if (m_reference.Cached.IsSet())
        {
            void* cached = ResolveInstanceByHandle(m_reference.Cached, RefCategory::Component);
            if (cached != nullptr)
            {
                return static_cast<ComponentBase*>(cached);
            }
        }
        if (m_reference.ComponentId == InvalidInstanceId)
        {
            return nullptr;
        }
        const ResolvedInstance resolved = ResolveInstanceById(
            m_reference.ObjectId,
            m_reference.ComponentId,
            RefCategory::Component);
        if (resolved.Pointer == nullptr)
        {
            return nullptr;
        }
        m_reference.Cached = resolved.Handle;
        return static_cast<ComponentBase*>(resolved.Pointer);
    }
}
