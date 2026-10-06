#include <JBro/Framework2D/Service/DebugDraw2DService.h>

#include <JBro/Runtime/DebugLineBatch.h>

#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    namespace
    {
        constexpr UInt32 CircleSegments = 32;

        void ArrowHead(Internal::DebugLineBatch& batch, Vector2 from, Vector2 to)
        {
            const Float dx = to.x - from.x;
            const Float dy = to.y - from.y;
            const Float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.0f)
            {
                return;
            }
            // 촉은 선을 25° 씩 양쪽으로 되돌린 두 선이다.
            const Float head = length * 0.25f;
            const Float ux = dx / length;
            const Float uy = dy / length;
            const Float cosine = 0.906307787f;
            const Float sine = 0.422618262f;
            const Float leftX = -(ux * cosine - uy * sine) * head;
            const Float leftY = -(ux * sine + uy * cosine) * head;
            const Float rightX = -(ux * cosine + uy * sine) * head;
            const Float rightY = -(-ux * sine + uy * cosine) * head;
            batch.Add(to.x, to.y, 0.0f, to.x + leftX, to.y + leftY, 0.0f);
            batch.Add(to.x, to.y, 0.0f, to.x + rightX, to.y + rightY, 0.0f);
        }
    }

    void DebugDraw2DService::Line(Vector2 from, Vector2 to, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        batch.Add(from.x, from.y, 0.0f, to.x, to.y, 0.0f);
    }

    void DebugDraw2DService::Ray(Vector2 origin, Vector2 direction, const Color& color, Float duration, Float thickness) const
    {
        Line(origin, Vector2{origin.x + direction.x, origin.y + direction.y}, color, duration, thickness);
    }

    void DebugDraw2DService::Arrow(Vector2 from, Vector2 to, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        batch.Add(from.x, from.y, 0.0f, to.x, to.y, 0.0f);
        ArrowHead(batch, from, to);
    }

    void DebugDraw2DService::Rect(Vector2 center, Vector2 size, Float angle, const Color& color, Float duration, Float thickness) const
    {
        const Float cosine = std::cos(angle);
        const Float sine = std::sin(angle);
        const Float halfX = size.x * 0.5f;
        const Float halfY = size.y * 0.5f;
        const Float localX[4] = {-halfX, halfX, halfX, -halfX};
        const Float localY[4] = {-halfY, -halfY, halfY, halfY};
        Vector2 corners[4];
        for (Int32 index = 0; index < 4; ++index)
        {
            corners[index].x = center.x + localX[index] * cosine - localY[index] * sine;
            corners[index].y = center.y + localX[index] * sine + localY[index] * cosine;
        }
        Polygon(corners, 4, true, color, duration, thickness);
    }

    void DebugDraw2DService::Circle(Vector2 center, Float radius, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        Float previousX = center.x + radius;
        Float previousY = center.y;
        for (UInt32 segment = 1; segment <= CircleSegments; ++segment)
        {
            const Float angle = TwoPi * static_cast<JBro::Float>(segment) / static_cast<JBro::Float>(CircleSegments);
            const Float x = center.x + std::cos(angle) * radius;
            const Float y = center.y + std::sin(angle) * radius;
            batch.Add(previousX, previousY, 0.0f, x, y, 0.0f);
            previousX = x;
            previousY = y;
        }
    }

    void DebugDraw2DService::Polygon(const Vector2* points, UInt32 count, Bool closed, const Color& color, Float duration,
        Float thickness) const
    {
        if (points == nullptr || count < 2)
        {
            return;
        }
        Internal::DebugLineBatch batch(color, duration, thickness);
        for (UInt32 index = 0; index + 1 < count; ++index)
        {
            batch.Add(points[index].x, points[index].y, 0.0f, points[index + 1].x, points[index + 1].y, 0.0f);
        }
        if (closed && count > 2)
        {
            batch.Add(points[count - 1].x, points[count - 1].y, 0.0f, points[0].x, points[0].y, 0.0f);
        }
    }

    void DebugDraw2DService::Cross(Vector2 at, Float size, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        const Float arm = size * 0.70710678f;
        batch.Add(at.x - arm, at.y - arm, 0.0f, at.x + arm, at.y + arm, 0.0f);
        batch.Add(at.x - arm, at.y + arm, 0.0f, at.x + arm, at.y - arm, 0.0f);
    }
}
