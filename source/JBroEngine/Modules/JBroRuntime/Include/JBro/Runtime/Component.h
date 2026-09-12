#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/SafePtr.h>

#include <type_traits>

namespace JBro
{
    class Canvas;
    class GameObject;
    class GameScriptBase;

    namespace Internal
    {
        class CanvasAccess;
    }

    // 모든 컴포넌트의 다형성 베이스. 파생 타입은 반드시
    //   static constexpr const char* StaticTypeName() { return "..."; }
    // 을 제공하고 GetTypeId() 를 그것으로 구현한다.
    class ComponentBase : public EnableSafeFromThis<ComponentBase>
    {
    public:
        virtual ~ComponentBase() = default;

        virtual ComponentTypeId GetTypeId() const = 0;

        // 스크립트 DLL 이 파생하는 타입의 vtable 은 ABI 다. 이 집합은 D-48 로 고정했고
        // 추가는 Decisions 와 D-28 재빌드 규약을 거친다.
        //
        // OnAttached 는 소유 오브젝트와 식별자가 확정된 직후다. 형제 컴포넌트 캐시를 잡는 자리이며,
        // 매 프레임 조회를 없애는 것이 이 훅의 존재 이유다(§9).
        // OnDetached 는 풀에 반납되기 직전이다. GameScriptBase 의 OnCreate 는 OnAttached 뒤에,
        // OnDestroy 는 OnDetached 앞에 온다.
        virtual void OnAttached();
        virtual void OnDetached();
        virtual void OnEnabled();
        virtual void OnDisabled();

        InstanceId       GetInstanceId() const;
        InstanceHandle   GetHandle() const;
        // 스크립트 표면이므로 소유 오브젝트는 핸들로 준다. 실 객체는 엔진 계층만 본다.
        GameObjectHandle GetOwner() const;

        // §8.1 단일 활성 게이트. 모든 시스템이 이 함수 하나만 본다.
        bool IsActiveComponent() const;
        bool IsEnabled() const;
        void SetEnabled(bool enabled);

    private:
        friend class Canvas;
        friend class GameObject;
        friend class Internal::CanvasAccess;

        GameObject* GetOwnerObject() const;
        void SetOwner(GameObject* owner);
        void SetInstanceIdentity(InstanceId instanceId, InstanceHandle handle);

        SafePtr<GameObject> m_owner;
        InstanceId          m_instanceId = InvalidInstanceId;
        InstanceHandle      m_handle;
        bool                m_enabled = true;
    };

    template<typename T>
        requires (std::is_base_of_v<ComponentBase, T> && !std::is_base_of_v<GameScriptBase, T>)
    struct RefCategoryOf<T>
    {
        static constexpr RefCategory value = RefCategory::Component;
    };
}
