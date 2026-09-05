#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/SafePtr.h>

#include <type_traits>

namespace JBro
{
    class Canvas;
    class GameObject;

    // 모든 컴포넌트의 다형성 베이스. 파생 타입은 반드시
    //   static constexpr const char* StaticTypeName() { return "..."; }
    // 을 제공하고 GetTypeId() 를 그것으로 구현한다.
    class ComponentBase : public EnableSafeFromThis<ComponentBase>
    {
    public:
        virtual ~ComponentBase() = default;

        virtual ComponentTypeId GetTypeId() const = 0;

        InstanceId     GetInstanceId() const;
        InstanceHandle GetHandle() const;
        GameObject*    GetOwner() const;

        // §8.1 단일 활성 게이트. 모든 시스템이 이 함수 하나만 본다.
        bool IsActiveComponent() const;
        bool IsEnabled() const;
        void SetEnabled(bool enabled);

    private:
        friend class Canvas;
        friend class GameObject;

        void SetOwner(GameObject* owner);
        void SetInstanceIdentity(InstanceId instanceId, InstanceHandle handle);

        SafePtr<GameObject> m_owner;
        InstanceId          m_instanceId = InvalidInstanceId;
        InstanceHandle      m_handle;
        bool                m_enabled = true;
    };

    template<typename T>
        requires std::is_base_of_v<ComponentBase, T>
    struct RefCategoryOf<T>
    {
        static constexpr RefCategory value = RefCategory::Component;
    };
}
