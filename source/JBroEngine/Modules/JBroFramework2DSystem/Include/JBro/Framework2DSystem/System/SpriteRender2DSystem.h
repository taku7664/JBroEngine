#pragma once

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>
#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class SpriteRender2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // 스프라이트 에셋 핸들을 렌더러 텍스처와 UV 로 푸는 곳이다. 없으면 모든 스프라이트가 흰색(틴트)이다.
        void SetSpriteLibrary(SpriteLibrary* library);
        // Appends to caller-reserved storage after transform update and BeginFrame.
        // Overflow is reported by RenderWorld2D::GetDroppedSpriteCount().
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
        SpriteLibrary* m_spriteLibrary = nullptr;
    };
}
