#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class Transform2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;
    };
}
