#include <JBro/Framework3D/Service/DebugDraw3DService.h>

#include <JBro/Runtime/DebugLineBatch.h>

#include <cmath>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    namespace
    {
        constexpr UInt32 CircleSegments = 32;

        void AddLine(Internal::DebugLineBatch& batch, const Vector3& from, const Vector3& to)
        {
            batch.Add(from.x, from.y, from.z, to.x, to.y, to.z);
        }

        // n 에 수직인 단위 벡터 둘이다. n 이 0 이면 x·y 다.
        void Perpendiculars(const Vector3& normal, Vector3& first, Vector3& second)
        {
            const Vector3 unit = Normalize(normal);
            if (Length(unit) <= 0.0f)
            {
                first = Vector3{1.0f, 0.0f, 0.0f};
                second = Vector3{0.0f, 1.0f, 0.0f};
                return;
            }
            // n 과 가장 덜 나란한 축으로 첫 수직을 만든다 - 나란한 축과 외적하면 0 이 된다.
            const Vector3 helper = std::fabs(unit.x) < 0.9f ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
            first = Normalize(Cross(unit, helper));
            second = Cross(unit, first);
        }

        void AddCircle(Internal::DebugLineBatch& batch, const Vector3& center, const Vector3& axisA, const Vector3& axisB, Float radius)
        {
            Vector3 previous = Add(center, Scale(axisA, radius));
            for (UInt32 segment = 1; segment <= CircleSegments; ++segment)
            {
                const Float angle = TwoPi * static_cast<JBro::Float>(segment) / static_cast<JBro::Float>(CircleSegments);
                const Vector3 point = Add(center, Add(Scale(axisA, std::cos(angle) * radius), Scale(axisB, std::sin(angle) * radius)));
                AddLine(batch, previous, point);
                previous = point;
            }
        }
    }

    void DebugDraw3DService::Line(const Vector3& from, const Vector3& to, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddLine(batch, from, to);
    }

    void DebugDraw3DService::Ray(const Vector3& origin, const Vector3& direction, const Color& color, Float duration, Float thickness) const
    {
        Line(origin, Add(origin, direction), color, duration, thickness);
    }

    void DebugDraw3DService::Arrow(const Vector3& from, const Vector3& to, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddLine(batch, from, to);
        const Vector3 direction = Subtract(to, from);
        const Float length = Length(direction);
        if (length <= 0.0f)
        {
            return;
        }
        Vector3 sideA;
        Vector3 sideB;
        Perpendiculars(direction, sideA, sideB);
        const Vector3 back = Scale(direction, -0.25f * 0.906307787f);
        const Float spread = length * 0.25f * 0.422618262f;
        AddLine(batch, to, Add(to, Add(back, Scale(sideA, spread))));
        AddLine(batch, to, Add(to, Add(back, Scale(sideA, -spread))));
        AddLine(batch, to, Add(to, Add(back, Scale(sideB, spread))));
        AddLine(batch, to, Add(to, Add(back, Scale(sideB, -spread))));
    }

    void DebugDraw3DService::Box(const Vector3& center, const Vector3& halfExtents, const Quaternion& rotation, const Color& color,
        Float duration, Float thickness) const
    {
        Vector3 corners[8];
        for (Int32 index = 0; index < 8; ++index)
        {
            const Vector3 local{
                (index & 1) != 0 ? halfExtents.x : Float(-halfExtents.x),
                (index & 2) != 0 ? halfExtents.y : Float(-halfExtents.y),
                (index & 4) != 0 ? halfExtents.z : Float(-halfExtents.z)};
            corners[index] = Add(center, Rotate(rotation, local));
        }
        Internal::DebugLineBatch batch(color, duration, thickness);
        // 번호의 한 비트만 다른 두 꼭짓점이 모서리다. 비트마다 넷씩, 열둘이다.
        for (Int32 index = 0; index < 8; ++index)
        {
            for (Int32 bit = 1; bit < 8; bit <<= 1)
            {
                if ((index & bit) == 0)
                {
                    AddLine(batch, corners[index], corners[index | bit]);
                }
            }
        }
    }

    void DebugDraw3DService::Sphere(const Vector3& center, Float radius, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        const Vector3 x{1.0f, 0.0f, 0.0f};
        const Vector3 y{0.0f, 1.0f, 0.0f};
        const Vector3 z{0.0f, 0.0f, 1.0f};
        AddCircle(batch, center, x, y, radius);
        AddCircle(batch, center, y, z, radius);
        AddCircle(batch, center, z, x, radius);
    }

    void DebugDraw3DService::Circle(const Vector3& center, const Vector3& normal, Float radius, const Color& color, Float duration,
        Float thickness) const
    {
        Vector3 axisA;
        Vector3 axisB;
        Perpendiculars(normal, axisA, axisB);
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddCircle(batch, center, axisA, axisB, radius);
    }

    void DebugDraw3DService::Axes(const Vector3& position, const Quaternion& rotation, Float size, Float duration, Float thickness) const
    {
        Line(position, Add(position, Scale(Rotate(rotation, Vector3{1.0f, 0.0f, 0.0f}), size)), Color{1.0f, 0.2f, 0.2f, 1.0f}, duration,
            thickness);
        Line(position, Add(position, Scale(Rotate(rotation, Vector3{0.0f, 1.0f, 0.0f}), size)), Color{0.2f, 1.0f, 0.2f, 1.0f}, duration,
            thickness);
        Line(position, Add(position, Scale(Rotate(rotation, Vector3{0.0f, 0.0f, 1.0f}), size)), Color{0.3f, 0.5f, 1.0f, 1.0f}, duration,
            thickness);
    }

    void DebugDraw3DService::Cross(const Vector3& at, Float size, const Color& color, Float duration, Float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddLine(batch, Vector3{at.x - size, at.y, at.z}, Vector3{at.x + size, at.y, at.z});
        AddLine(batch, Vector3{at.x, at.y - size, at.z}, Vector3{at.x, at.y + size, at.z});
        AddLine(batch, Vector3{at.x, at.y, at.z - size}, Vector3{at.x, at.y, at.z + size});
    }
}
