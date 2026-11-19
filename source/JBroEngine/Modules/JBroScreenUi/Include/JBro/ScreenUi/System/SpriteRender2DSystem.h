#pragma once

#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/ScreenUi/Rendering/SpriteLibrary.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Graphics/MaterialLibrary.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    class SpriteRender2DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // 스프라이트 에셋 핸들을 렌더러 텍스처와 UV 로 푸는 곳이다. 없으면 모든 스프라이트가 흰색(틴트)이다.
        void SetSpriteLibrary(SpriteLibrary* library);
        // 머티리얼 에셋을 렌더러 머티리얼로 푸는 곳이다(D-347). 없으면 모든 스프라이트가 빌트인 셰이더다.
        void SetMaterialLibrary(MaterialLibrary* library);
        // Appends to caller-reserved storage after transform update and BeginFrame.
        // Overflow is reported by RenderWorld2D::GetDroppedSpriteCount().
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
        SpriteLibrary* m_spriteLibrary = nullptr;
        MaterialLibrary* m_materialLibrary = nullptr;
    };
}
