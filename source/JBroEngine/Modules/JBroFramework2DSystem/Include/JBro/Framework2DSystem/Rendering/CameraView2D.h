#pragma once

#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>

namespace JBro
{
    // 카메라 하나가 화면에 보이는 것이다(D-239).
    struct CameraView2D
    {
        // 월드 → 뷰. `PixelPerfect` 면 이동이 원본 1 픽셀(`1 / pixelsPerUnit`)에 맞춰져 있다.
        Matrix3x2 view;
        // 그려지는 사각형(`GetScreenArea`)이 담는 반폭·반높이다(뷰 공간 유닛).
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
    };

    // 이 카메라로 그릴 수 있는 값인가. 투영에 맞는 크기(`Orthographic` 은 `orthographicSize`, `PixelPerfect` 는 `pixelsPerUnit`)가
    // 유한한 양수이고 `nearPlane < farPlane` 이어야 한다. 그릴 수 없는 카메라는 고르지 않는다 - 캔버스의 값이 잘못된 것은
    // 장치 오류가 아니므로 프레임을 실패시키지 않는다.
    bool IsDrawableCamera2D(const RenderCamera2D& camera);

    // `PixelPerfect` 카메라면 `frame` 의 그려지는 사각형을 레터박스로 건다(`ComputePixelPerfectArea`). 아니면(널 포함) 사각형을
    // 비워 대상 전체로 둔다. 화면 레이어도 이 사각형 안에 그려지고 포인터도 이 사각형으로 역투영된다.
    void ApplyCameraArea(const RenderCamera2D* camera, ScreenSpaceFrame& frame);

    // **카메라가 화면에 무엇을 보이는지는 이것 하나로 잰다.** 그리기(렌더 브리지)·버튼의 월드 역투영·에디터의 레이어 공간
    // 바꾸기가 함께 쓴다 - 따로 계산하면 레터박스가 한 곳에만 들어가 그린 자리와 누르는 자리가 어긋난다.
    //   Orthographic  반높이 = orthographicSize, 반폭 = 반높이 x 그려지는 사각형의 가로세로비
    //   PixelPerfect  반폭·반높이 = 기준 해상도 / (2 x pixelsPerUnit), 뷰의 이동을 원본 1 픽셀에 맞춘다
    // 그릴 수 없는 카메라이거나 대상·기준이 쓸 수 없으면 거짓이고 결과를 건드리지 않는다.
    bool ComputeCameraView2D(const RenderCamera2D& camera, const ScreenSpaceFrame& frame, CameraView2D& result);
}
