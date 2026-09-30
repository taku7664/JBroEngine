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

    namespace Object
    {
        class GameObject;
    }

    namespace Internal
    {
        class CanvasAccess;
    }

    // 게임 스크립트의 다형성 베이스다. **컴포넌트가 아니다**(D-271).
    //
    // 오브젝트는 컴포넌트 목록과 스크립트 목록을 따로 들고, 스크립트는 제 소유자·번호·켜짐을 스스로 든다.
    // 빌트인 컴포넌트는 엔진이 소유하는 데이터이고 스크립트에는 핸들로만 보인다. 스크립트는 사용자 코드라
    // `Ref<T>` 로 가리키고 포인터로 부른다. 둘을 한 베이스에 두면 "붙은 것" 을 다루는 모든 자리가
    // 둘을 다시 가려내야 했다 - 실행 순서·훅 발송·저장·핫 리로드가 모두 그랬다.
    //
    // 파생 타입은 `JBRO_SCRIPT_BODY` 가 타입 이름과 `GetTypeId()` 를 낸다. 가상 함수 표는 스크립트 DLL 과의
    // ABI 다(D-48). 바꾸면 `ServiceContextAbiVersion` 을 올린다.
    class GameScriptBase : public EnableSafeFromThis<GameScriptBase>
    {
    public:
        virtual ~GameScriptBase() = default;

        virtual ComponentTypeId GetTypeId() const = 0;

        // OnAttached 는 소유 오브젝트와 번호가 정해진 직후, OnDetached 는 풀에 돌려주기 직전이다.
        // OnCreate 는 OnAttached 뒤에, OnDestroy 는 OnDetached 앞에 온다.
        virtual void OnAttached();
        virtual void OnDetached();
        virtual void OnEnabled();
        virtual void OnDisabled();

        virtual void OnCreate();
        virtual void OnStart();
        // 델타는 인자로 오지 않는다(ProjectRule §7, D-242). `GetServiceContext().Time.DeltaTime()` 으로 읽는다 -
        // `OnFixedUpdate` 안에서 읽으면 고정 델타다.
        virtual void OnUpdate();
        virtual void OnFixedUpdate();
        virtual void OnDestroy();

        InstanceId       GetInstanceId() const;
        InstanceHandle   GetHandle() const;
        // 붙일 때 `GetTypeId()` 를 한 번 불러 둔 값이다. 오브젝트의 스크립트 목록을 타입으로 훑을 때 원소마다 가상 호출을 하지 않는다(§9).
        ComponentTypeId  GetCachedTypeId() const;
        // 스크립트 표면이므로 소유 오브젝트는 핸들로 준다.
        Handle::GameObject GetGameObject() const;

        // 켜져 있고 소유 오브젝트가 계층에서 활성인가. 스크립트 시스템이 훅마다 이것 하나만 본다.
        bool IsActiveScript() const;
        bool IsEnabled() const;
        void SetEnabled(bool enabled);

    private:
        friend class JBro::Canvas;
        friend class Object::GameObject;
        friend class Internal::CanvasAccess;

        Object::GameObject* GetOwnerObject() const;
        void SetOwner(Object::GameObject* owner);
        void CacheTypeId();
        void SetInstanceIdentity(InstanceId instanceId, InstanceHandle handle);

        SafePtr<Object::GameObject> m_owner;
        ComponentTypeId             m_typeId = InvalidComponentTypeId;
        InstanceId                  m_instanceId = InvalidInstanceId;
        InstanceHandle              m_handle;
        bool                        m_enabled = true;
    };

    template<typename T>
        requires std::is_base_of_v<GameScriptBase, T>
    struct RefCategoryOf<T>
    {
        static constexpr RefCategory value = RefCategory::Script;
    };
}
