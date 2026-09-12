#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class ScriptSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnInitialize (Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;
        void OnUpdate     (Canvas& canvas, float deltaTime) override;
        void OnShutdown   (Canvas& canvas) override;
    };
}
