#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    // 사용자 스크립트의 베이스. ComponentBase 를 상속하고 스크립트 전용 훅을 노출한다.
    class GameScript : public ComponentBase
    {
    public:
        virtual ~GameScript() = default;

        GameObject* GetGameObject() const { return GetOwner(); }

        virtual void OnCreate() {}
        virtual void OnStart () {}
        virtual void OnUpdate      (float deltaTime)          { (void)deltaTime; }
        virtual void OnFixedUpdate (float fixedDeltaTime)     { (void)fixedDeltaTime; }
        virtual void OnCollisionEnter(const Collision2D& hit) { (void)hit; }
        virtual void OnCollisionExit (const Collision2D& hit) { (void)hit; }
        virtual void OnDestroy() {}
    };
}
