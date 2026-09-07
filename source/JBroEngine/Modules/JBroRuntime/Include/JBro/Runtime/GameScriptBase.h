#pragma once

#include <JBro/Runtime/Component.h>

namespace JBro
{
    // Dimension-independent hooks. Collision contracts belong to each framework.
    class GameScriptBase : public ComponentBase
    {
    public:
        ~GameScriptBase() override = default;

        GameObject* GetGameObject() const;

        virtual void OnCreate();
        virtual void OnStart();
        virtual void OnUpdate(float deltaTime);
        virtual void OnFixedUpdate(float fixedDeltaTime);
        virtual void OnDestroy();
    };
}
