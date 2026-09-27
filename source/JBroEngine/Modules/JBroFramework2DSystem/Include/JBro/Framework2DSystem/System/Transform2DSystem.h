#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Types/Math2D.h>

namespace JBro::System
{
    class Transform2DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;
        // 이번 프레임의 화면 기준이다(D-237). 화면 레이어의 루트는 앵커 점을 부모로 삼는다. 프레임워크가 갱신 전에 넣는다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        ScreenSpaceFrame m_screen;
    };
}
