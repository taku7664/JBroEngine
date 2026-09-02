#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::Engine
{
    class GameScript : public Component
    {
    public:
        virtual ~GameScript() = default;

        GameObject GetGameObject() const;

        virtual void OnCreate();
        virtual void OnStart();
        virtual void OnUpdate(float deltaTime);
        virtual void OnFixedUpdate(float fixedDeltaTime);
        virtual void OnCollisionEnter(const Collision2D& collision);
        virtual void OnCollisionExit(const Collision2D& collision);
        virtual void OnDestroy();

    protected:
        GameScript(GameObject owner, TypeId typeId);
    };
}
