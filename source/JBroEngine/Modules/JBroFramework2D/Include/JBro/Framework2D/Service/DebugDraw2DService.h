#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Types/Color.h>

#include <cstdint>

namespace JBro::Service
{
    // 스크립트가 2D 월드에 디버그 도형을 그리는 표면이다(D-242, 기존 `IDebugDraw2D`).
    //
    //     const auto& debug = GetFramework2DServices().DebugDraw;
    //     debug.Line(from, to);                                   // 흰 선, 한 프레임, 1 픽셀
    //     debug.Circle(center, radius, Color{1, 0, 0, 1}, 2.0f);  // 빨간 원을 게임 시간 2 초 동안
    //
    // `duration` 은 게임 시간(초)이고 0 이면 한 프레임이다 - `OnFixedUpdate` 에서 그린 0 초짜리는 다음 고정 스텝까지 남는다.
    // `thickness` 는 화면 픽셀이다(0.25~64). 캔버스 뷰를 당겨도 같은 굵기다.
    // **그리기만 있다.** 비우기와 읽기는 엔진의 것이다. 캔버스 뷰에는 늘 보이고, 게임 뷰는 에디터의 토글, 게임 실행은 프로젝트의
    // `DebugModeEnabled` 를 따른다. Main-thread only. 가상 함수가 없다. 디버그 드로 시스템이 묶이지 않았으면 아무 일도 하지 않는다.
    class DebugDraw2DService
    {
    public:
        void Line(Vec2 from, Vec2 to, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        // origin 에서 origin + direction 까지다. 방향의 길이가 선의 길이다.
        void Ray(Vec2 origin, Vec2 direction, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        // 끝에 촉이 달린 선이다. 촉의 길이는 선 길이의 1/4 이다.
        void Arrow(Vec2 from, Vec2 to, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        // 가운데·크기·각(라디안)의 사각형 테두리다.
        void Rect(Vec2 center, Vec2 size, float angle = 0.0f, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f},
            float duration = 0.0f, float thickness = 1.0f) const;
        // 원 테두리다. 32 조각이다.
        void Circle(Vec2 center, float radius, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        // 점들을 차례로 잇는다. closed 면 끝과 처음도 잇는다. 점이 둘보다 적으면 아무 일도 없다.
        void Polygon(const Vec2* points, std::uint32_t count, bool closed = true, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f},
            float duration = 0.0f, float thickness = 1.0f) const;
        // 한 점에 ✕ 표를 둔다. size 는 팔 하나의 길이(월드)다.
        void Cross(Vec2 at, float size, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
    };
}
