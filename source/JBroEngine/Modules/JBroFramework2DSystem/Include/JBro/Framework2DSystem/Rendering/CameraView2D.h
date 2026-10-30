#pragma once

#include <JBro/ScreenUi/Rendering/Parallax2D.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Types/Math2D.h>
#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    // 카메라 하나가 화면에 보이는 것이다(D-239).
    struct CameraView2D
    {
        // 월드 → 뷰. `PixelPerfect` 면 이동이 원본 1 픽셀(`1 / pixelsPerUnit`)에 맞춰져 있다.
        Matrix3x2 view;
        // 그려지는 사각형(`GetScreenArea`)이 담는 반폭·반높이다(뷰 공간 유닛).
        Float halfWidth = 0.0f;
        Float halfHeight = 0.0f;
    };

    // 이 카메라로 그릴 수 있는 값인가. 투영에 맞는 크기(`Orthographic` 은 `orthographicSize`, `PixelPerfect` 는 `pixelsPerUnit`)가
    // 유한한 양수이고 `nearPlane < farPlane` 이어야 한다. 그릴 수 없는 카메라는 고르지 않는다 - 캔버스의 값이 잘못된 것은
    // 장치 오류가 아니므로 프레임을 실패시키지 않는다.
    Bool IsDrawableCamera2D(const RenderCamera2D& camera);

    // 뷰포트가 대상 전체인가(D-359). 비율이 0 이하·수가 아니면 거짓이다(그릴 수 없다).
    Bool HasFullViewport(const RenderCamera2D& camera);
    Bool HasDrawableViewport(const RenderCamera2D& camera);
    // **카메라가 그리는 사각형**(D-359)을 `frame` 에 건다 - 뷰포트(대상의 0..1 비율, 왼쪽 위 원점)를 대상 픽셀로 옮기고, `PixelPerfect` 면 그 안에서 레터박스를 센다.
    // 뷰마다 하나씩 부른다. 그릴 수 없는 카메라는 사각형을 비운다(대상 전체).
    void ApplyCameraViewport(const RenderCamera2D& camera, ScreenSpaceFrame& frame);
    // 화면 레이어·앵커·포인터의 화면 기준이 쓰는 사각형이다. 카메라의 뷰포트가 **대상 전체일 때만** `ApplyCameraViewport` 와 같고(`PixelPerfect` 의 레터박스),
    // 뷰포트가 일부이거나 카메라가 없으면 사각형을 비워 대상 전체로 둔다 - 분할 화면에서 HUD 는 화면 전체다.
    void ApplyCameraArea(const RenderCamera2D* camera, ScreenSpaceFrame& frame);

    // **카메라가 화면에 무엇을 보이는지는 이것 하나로 잰다.** 그리기(렌더 브리지)·버튼의 월드 역투영·에디터의 레이어 공간
    // 바꾸기가 함께 쓴다 - 따로 계산하면 레터박스가 한 곳에만 들어가 그린 자리와 누르는 자리가 어긋난다.
    //   Orthographic  반높이 = orthographicSize, 반폭 = 반높이 x 그려지는 사각형의 가로세로비
    //   PixelPerfect  반폭·반높이 = 기준 해상도 / (2 x pixelsPerUnit), 뷰의 이동을 원본 1 픽셀에 맞춘다
    // 그릴 수 없는 카메라이거나 대상·기준이 쓸 수 없으면 거짓이고 결과를 건드리지 않는다.
    Bool ComputeCameraView2D(const RenderCamera2D& camera, const ScreenSpaceFrame& frame, CameraView2D& result);

    // **카메라가 담는 영역의 월드 네 귀퉁이**다(D-303). 에디터 캔버스 뷰가 카메라 영역을 그릴 때 쓴다. 크기는 `ComputeCameraView2D` 이고,
    // `world` 는 카메라 오브젝트의 월드 행렬이라 회전·크기가 그대로 얹힌다(뷰는 그 역행렬이다). 차례는 왼아래·오른아래·오른위·왼위다.
    // `PixelPerfect` 의 원본 1 픽셀 맞춤(뷰 이동의 반올림)은 넣지 않는다 - 원본 픽셀 반 칸보다 작다. 그릴 수 없으면 거짓이다.
    Bool ComputeCameraWorldArea2D(const RenderCamera2D& camera, const Matrix3x2& world, const ScreenSpaceFrame& frame,
        Vector2 (&corners)[4]);

}
