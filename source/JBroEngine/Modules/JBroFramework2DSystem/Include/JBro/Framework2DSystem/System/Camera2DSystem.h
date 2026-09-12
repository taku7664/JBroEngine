#pragma once

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class Camera2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // Caller begins/ends the frame; transforms must be updated before extraction.
        // Selects the first active primary camera with an invertible world transform.
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
    };
}
