#include <JBro/Framework2D/Service/DebugDraw2DService.h>

#include <JBro/Runtime/DebugLineBatch.h>

#include <cmath>

namespace JBro::Service
{
    namespace
    {
        constexpr std::uint32_t CircleSegments = 32;

        void ArrowHead(Internal::DebugLineBatch& batch, Vec2 from, Vec2 to)
        {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.0f)
            {
                return;
            }
            // 촉은 선을 25° 씩 양쪽으로 되돌린 두 선이다.
            const float head = length * 0.25f;
            const float ux = dx / length;
            const float uy = dy / length;
            const float cosine = 0.906307787f;
            const float sine = 0.422618262f;
            const float leftX = -(ux * cosine - uy * sine) * head;
            const float leftY = -(ux * sine + uy * cosine) * head;
            const float rightX = -(ux * cosine + uy * sine) * head;
            const float rightY = -(-ux * sine + uy * cosine) * head;
            batch.Add(to.x, to.y, 0.0f, to.x + leftX, to.y + leftY, 0.0f);
            batch.Add(to.x, to.y, 0.0f, to.x + rightX, to.y + rightY, 0.0f);
        }
    }

    void DebugDraw2DService::Line(Vec2 from, Vec2 to, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        batch.Add(from.x, from.y, 0.0f, to.x, to.y, 0.0f);
    }

    void DebugDraw2DService::Ray(Vec2 origin, Vec2 direction, const Color& color, float duration, float thickness) const
    {
        Line(origin, Vec2{origin.x + direction.x, origin.y + direction.y}, color, duration, thickness);
    }

    void DebugDraw2DService::Arrow(Vec2 from, Vec2 to, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        batch.Add(from.x, from.y, 0.0f, to.x, to.y, 0.0f);
        ArrowHead(batch, from, to);
    }

    void DebugDraw2DService::Rect(Vec2 center, Vec2 size, float angle, const Color& color, float duration, float thickness) const
    {
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float halfX = size.x * 0.5f;
        const float halfY = size.y * 0.5f;
        const float localX[4] = {-halfX, halfX, halfX, -halfX};
        const float localY[4] = {-halfY, -halfY, halfY, halfY};
        Vec2 corners[4];
        for (int index = 0; index < 4; ++index)
        {
            corners[index].x = center.x + localX[index] * cosine - localY[index] * sine;
            corners[index].y = center.y + localX[index] * sine + localY[index] * cosine;
        }
        Polygon(corners, 4, true, color, duration, thickness);
    }

    void DebugDraw2DService::Circle(Vec2 center, float radius, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        float previousX = center.x + radius;
        float previousY = center.y;
        for (std::uint32_t segment = 1; segment <= CircleSegments; ++segment)
        {
            const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(CircleSegments);
            const float x = center.x + std::cos(angle) * radius;
            const float y = center.y + std::sin(angle) * radius;
            batch.Add(previousX, previousY, 0.0f, x, y, 0.0f);
            previousX = x;
            previousY = y;
        }
    }

    void DebugDraw2DService::Polygon(const Vec2* points, std::uint32_t count, bool closed, const Color& color, float duration,
        float thickness) const
    {
        if (points == nullptr || count < 2)
        {
            return;
        }
        Internal::DebugLineBatch batch(color, duration, thickness);
        for (std::uint32_t index = 0; index + 1 < count; ++index)
        {
            batch.Add(points[index].x, points[index].y, 0.0f, points[index + 1].x, points[index + 1].y, 0.0f);
        }
        if (closed && count > 2)
        {
            batch.Add(points[count - 1].x, points[count - 1].y, 0.0f, points[0].x, points[0].y, 0.0f);
        }
    }

    void DebugDraw2DService::Cross(Vec2 at, float size, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        const float arm = size * 0.70710678f;
        batch.Add(at.x - arm, at.y - arm, 0.0f, at.x + arm, at.y + arm, 0.0f);
        batch.Add(at.x - arm, at.y + arm, 0.0f, at.x + arm, at.y - arm, 0.0f);
    }
}
