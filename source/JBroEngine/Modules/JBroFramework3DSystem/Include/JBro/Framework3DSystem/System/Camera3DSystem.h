#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro
{
    class RenderWorld3D;
}

namespace JBro::System
{
    // 주 카메라 하나를 렌더 월드에 뜬다. 여럿이 `primary` 면 먼저 만난 것이다(2D 와 같다).
    class Camera3DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;
        void SetRenderWorld(RenderWorld3D* renderWorld);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld3D* m_renderWorld = nullptr;
    };
}
