#pragma once

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class SpriteRender2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // Appends to caller-reserved storage after transform update and BeginFrame.
        // Overflow is reported by RenderWorld2D::GetDroppedSpriteCount().
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
    };
}
