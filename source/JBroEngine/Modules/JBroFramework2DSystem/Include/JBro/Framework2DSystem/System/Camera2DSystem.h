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
        // 그릴 카메라를 고른다: 켜진 주 카메라, 없으면 켜진 첫 카메라(월드 행렬이 뒤집히는 것만). 버튼의 역투영도 이것을 부른다(D-237) -
        // 그리는 카메라와 누르는 카메라가 달라지지 않는다.
        static bool SelectCamera(Canvas& canvas, RenderCamera2D& camera);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
    };
}
