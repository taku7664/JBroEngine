#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    class Transform2DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;
        // 이번 프레임의 화면 기준이다(D-237). 화면 레이어의 루트는 앵커 점을 부모로 삼는다. 프레임워크가 갱신 전에 넣는다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);
        // 뷰포트에 묶인 화면 레이어의 기준이다(D-439). 그 레이어의 루트는 그 기준에서 앵커를 잰다. 비면 모든 화면 레이어가 위의 기준이다.
        void SetScreenLayerFrames(ArrayView<const ScreenLayerFrame> frames);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        ScreenSpaceFrame m_screen;
        Array<ScreenLayerFrame> m_layerFrames;
    };
}
