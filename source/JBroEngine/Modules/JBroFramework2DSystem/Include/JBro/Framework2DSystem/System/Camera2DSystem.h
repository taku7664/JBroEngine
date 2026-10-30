#pragma once

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Component
{
    class Camera2D;
}

namespace JBro::System
{
    class Camera2DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // Caller begins/ends the frame; transforms must be updated before extraction.
        // Selects the first active primary camera with an invertible world transform.
        void ExtractRenderWorld(Canvas& canvas);
        // 그릴 카메라를 고른다: 켜진 주 카메라, 없으면 켜진 첫 카메라(월드 행렬이 뒤집히는 것만). 버튼의 역투영도 이것을 부른다(D-237) -
        // 그리는 카메라와 누르는 카메라가 달라지지 않는다.
        // **값이 잘못된 카메라는 건너뛴다**(D-239, `IsDrawableCamera2D`). `unusable` 을 주면 그렇게 건너뛴 수를 더한다.
        static Bool SelectCamera(Canvas& canvas, RenderCamera2D& camera, UInt32* unusable = nullptr);
        // **그 픽셀이 든 뷰포트의 카메라**(D-359, 분할 화면)다. 겹치면 `depth` 가 큰 것. 드는 뷰포트가 없으면 `SelectCamera` 다. `frame` 의 사각형은 비워서 준다.
        static Bool SelectCameraAt(Canvas& canvas, const ScreenSpaceFrame& frame, Float pixelX, Float pixelY, RenderCamera2D& camera);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        // 컴포넌트에서 그릴 카메라를 채운다. 꺼졌거나 트랜스폼이 없거나 그릴 수 없으면 거짓이다.
        static Bool FillCamera(Canvas& canvas, Component::Camera2D& camera, RenderCamera2D& item);

        RenderWorld2D* m_renderWorld = nullptr;
        // 지난 프레임에 건너뛴 카메라 수다. 늘 때만 경고한다 - 매 프레임 쓰면 로그가 그것으로 찬다.
        UInt32 m_warnedUnusable = 0;
    };
}
