#pragma once

#include <JBro/Framework2D/Rendering/RenderWorld2D.h>
#include <JBro/Runtime/GameSystem.h>

namespace JBro::System
{
    class Camera2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
    };
}
