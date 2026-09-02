#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>

namespace JBro
{
    class GameObject;

    // 모든 컴포넌트의 다형성 베이스. 파생 타입은 반드시
    //   static constexpr const char* StaticTypeName() { return "..."; }
    // 을 제공하고 GetTypeId() 를 그것으로 구현한다.
    class ComponentBase
    {
    public:
        virtual ~ComponentBase() = default;

        virtual ComponentTypeId GetTypeId() const = 0;

        GameObject* GetOwner() const;
        void        SetOwner(GameObject* owner);

        // §8.1 단일 활성 게이트. 모든 시스템이 이 함수 하나만 본다.
        bool IsActiveComponent() const;
        void SetEnabled(bool enabled);

    protected:
        GameObject* m_owner   = nullptr;
        bool        m_enabled = true;
    };
}
