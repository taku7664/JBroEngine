#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class RenderWorld3D;
}

namespace JBro::System
{
    // **3D 라이트를 렌더 월드에 담는다**(D-407, lighting3d-plan 2 단계). 메시 추출과 같은 차례(트랜스폼 뒤)다. 감춘 레이어의 라이트는 담지 않는다.
    // 자리가 없는(트랜스폼이 없거나 꺼진) 라이트는 환경광만 담는다. 담을 자리가 차면 버리고 렌더 월드가 센다.
    class Light3DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld3D* renderWorld);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld3D* m_renderWorld = nullptr;
    };
}
