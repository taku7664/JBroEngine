#pragma once

#include <JBro/Framework3D/Math3D.h>
#include <JBro/Types/Color.h>

namespace JBro::Service
{
    // 스크립트가 3D 월드에 디버그 도형을 그리는 표면이다(D-232). 기존 엔진에는 3D 디버그 드로가 없었다.
    //
    //     const auto& debug = GetFramework3DServices().DebugDraw;
    //     debug.Ray(eye, forward * 10.0f, Color{0, 1, 0, 1});
    //     debug.Box(center, halfExtents, rotation);
    //
    // 인자의 뜻은 2D 와 같다(`DebugDraw2DService`). 선은 메시에 가려진다 - 깊이를 보되 쓰지 않는다. Main-thread only. 가상 함수가 없다.
    class DebugDraw3DService
    {
    public:
        void Line(const Vec3& from, const Vec3& to, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        void Ray(const Vec3& origin, const Vec3& direction, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f},
            float duration = 0.0f, float thickness = 1.0f) const;
        // 촉은 선 길이의 1/4 이고, 선에 수직인 두 평면에 하나씩 네 갈래다.
        void Arrow(const Vec3& from, const Vec3& to, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        // 가운데·반 크기·회전의 상자 모서리 열둘이다.
        void Box(const Vec3& center, const Vec3& halfExtents, const Quaternion& rotation = Quaternion{},
            const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f, float thickness = 1.0f) const;
        // 세 축 평면의 대원 셋이다(32 조각씩).
        void Sphere(const Vec3& center, float radius, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
        // normal 에 수직인 평면의 원이다.
        void Circle(const Vec3& center, const Vec3& normal, float radius, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f},
            float duration = 0.0f, float thickness = 1.0f) const;
        // 회전의 세 축을 빨강(x)·초록(y)·파랑(z)으로 size 만큼 그린다.
        void Axes(const Vec3& position, const Quaternion& rotation, float size = 1.0f, float duration = 0.0f,
            float thickness = 1.0f) const;
        // 세 축 방향의 짧은 선 셋이다. size 는 팔 하나의 길이다.
        void Cross(const Vec3& at, float size, const Color& color = Color{1.0f, 1.0f, 1.0f, 1.0f}, float duration = 0.0f,
            float thickness = 1.0f) const;
    };
}
