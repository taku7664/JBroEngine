#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

// **평면 화면의 부드러운 카메라다**(D-252·D-324). 노드 그래프와 스프라이트 뷰어의 시트처럼 그림을 옮기고 키우는 에디터 화면이 같이 쓴다.
//
// 그리는 카메라와 가려는 카메라가 따로다. 손짓(팬·휠·맞춤)은 가려는 카메라만 바꾸고, 그리는 카메라가 프레임마다 그것을 따라간다.
// 판정과 그리기는 그리는 카메라로 한다 - 화면에 보이는 자리가 손에 잡히는 자리다. 캔버스 뷰(`CanvasViewPanel`)와 같은 손맛이다.
//
// 좌표는 둘이다. 내용 공간(그래프 자리·그림의 픽셀)과 화면 픽셀이다. 화면 = 원점 + (내용 - 팬) x 줌.
namespace JBro::ViewCamera
{
    // 캔버스 뷰의 `CameraSmoothSpeed`·`CameraSnapFraction` 과 같은 값이다. 화면마다 손맛이 다르면 안 된다.
    inline constexpr float SmoothSpeed = 10.0f;
    inline constexpr float SnapFraction = 0.0005f;

    struct Camera
    {
        // 화면 칸의 왼쪽 위 화면 자리다. 그릴 때마다 채운다.
        Vector2 origin;
        // 칸 왼쪽 위에 오는 내용 자리와 줌이다(그리는 카메라).
        Vector2 pan;
        Float zoom = 1.0f;
        // 가려는 카메라다.
        Vector2 goalPan;
        Float goalZoom = 1.0f;
    };

    Vector2 ToScreen(const Camera& camera, Vector2 content);
    Vector2 ToContent(const Camera& camera, Vector2 screen);
    // 두 카메라를 함께 그 자리로 놓는다(따라가지 않는다).
    void Set(Camera& camera, Vector2 pan, Float zoom);
    // 그리는 카메라가 가려는 카메라를 따라간다. `1 - exp(-속도 x 초)` 만큼 다가가 프레임 속도와 무관하다. 충분히 가까우면 붙인다.
    void Follow(Camera& camera, Float deltaSeconds);
    // 화면의 그 점 아래 내용 자리를 그대로 둔 채 가려는 줌을 바꾼다. 기준점은 가려는 카메라로 잰다 - 따라가는 도중에 휠을 거듭 돌려도
    // 같은 점을 붙잡는다.
    void ZoomAt(Camera& camera, Vector2 screen, Float zoom, Float minZoom, Float maxZoom);
    // 끈 화면 픽셀만큼 가려는 카메라를 민다(가려는 줌으로 내용 길이로 바꾼다).
    void PanByPixels(Camera& camera, Vector2 delta);
    // 내용 사각형이 칸 가운데에 다 보이게 가려는 카메라를 맞춘다. `immediate` 면 두 카메라를 함께 놓는다.
    void Fit(Camera& camera, const Rect& content, Vector2 viewSize, Float margin, Float minZoom, Float maxZoom, Bool immediate);
}
