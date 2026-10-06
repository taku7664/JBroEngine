#pragma once

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    // **2D 라이트를 렌더 월드에 담는다**(D-291). 스프라이트 추출과 같은 차례(트랜스폼 뒤)다. 감춘 레이어와 화면 레이어의 라이트는 담지 않는다 -
    // 화면 레이어는 빛을 받지도 내지도 않는다. 담을 자리가 차면 버리고 렌더 월드가 센다.
    class Light2DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
    };
}
