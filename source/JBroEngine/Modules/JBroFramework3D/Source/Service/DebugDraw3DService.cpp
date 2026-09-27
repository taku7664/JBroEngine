#include <JBro/Framework3D/Service/DebugDraw3DService.h>

#include <JBro/Runtime/DebugLineBatch.h>

#include <cmath>

namespace JBro::Service
{
    namespace
    {
        constexpr std::uint32_t CircleSegments = 32;

        void AddLine(Internal::DebugLineBatch& batch, const Vec3& from, const Vec3& to)
        {
            batch.Add(from.x, from.y, from.z, to.x, to.y, to.z);
        }

        // n 에 수직인 단위 벡터 둘이다. n 이 0 이면 x·y 다.
        void Perpendiculars(const Vec3& normal, Vec3& first, Vec3& second)
        {
            const Vec3 unit = Normalize(normal);
            if (Length(unit) <= 0.0f)
            {
                first = Vec3{1.0f, 0.0f, 0.0f};
                second = Vec3{0.0f, 1.0f, 0.0f};
                return;
            }
            // n 과 가장 덜 나란한 축으로 첫 수직을 만든다 - 나란한 축과 외적하면 0 이 된다.
            const Vec3 helper = std::fabs(unit.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
            first = Normalize(Cross(unit, helper));
            second = Cross(unit, first);
        }

        void AddCircle(Internal::DebugLineBatch& batch, const Vec3& center, const Vec3& axisA, const Vec3& axisB, float radius)
        {
            Vec3 previous = Add(center, Scale(axisA, radius));
            for (std::uint32_t segment = 1; segment <= CircleSegments; ++segment)
            {
                const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(CircleSegments);
                const Vec3 point = Add(center, Add(Scale(axisA, std::cos(angle) * radius), Scale(axisB, std::sin(angle) * radius)));
                AddLine(batch, previous, point);
                previous = point;
            }
        }
    }

    void DebugDraw3DService::Line(const Vec3& from, const Vec3& to, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddLine(batch, from, to);
    }

    void DebugDraw3DService::Ray(const Vec3& origin, const Vec3& direction, const Color& color, float duration, float thickness) const
    {
        Line(origin, Add(origin, direction), color, duration, thickness);
    }

    void DebugDraw3DService::Arrow(const Vec3& from, const Vec3& to, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddLine(batch, from, to);
        const Vec3 direction = Subtract(to, from);
        const float length = Length(direction);
        if (length <= 0.0f)
        {
            return;
        }
        Vec3 sideA;
        Vec3 sideB;
        Perpendiculars(direction, sideA, sideB);
        const Vec3 back = Scale(direction, -0.25f * 0.906307787f);
        const float spread = length * 0.25f * 0.422618262f;
        AddLine(batch, to, Add(to, Add(back, Scale(sideA, spread))));
        AddLine(batch, to, Add(to, Add(back, Scale(sideA, -spread))));
        AddLine(batch, to, Add(to, Add(back, Scale(sideB, spread))));
        AddLine(batch, to, Add(to, Add(back, Scale(sideB, -spread))));
    }

    void DebugDraw3DService::Box(const Vec3& center, const Vec3& halfExtents, const Quaternion& rotation, const Color& color,
        float duration, float thickness) const
    {
        Vec3 corners[8];
        for (int index = 0; index < 8; ++index)
        {
            const Vec3 local{
                (index & 1) != 0 ? halfExtents.x : -halfExtents.x,
                (index & 2) != 0 ? halfExtents.y : -halfExtents.y,
                (index & 4) != 0 ? halfExtents.z : -halfExtents.z};
            corners[index] = Add(center, Rotate(rotation, local));
        }
        Internal::DebugLineBatch batch(color, duration, thickness);
        // 번호의 한 비트만 다른 두 꼭짓점이 모서리다. 비트마다 넷씩, 열둘이다.
        for (int index = 0; index < 8; ++index)
        {
            for (int bit = 1; bit < 8; bit <<= 1)
            {
                if ((index & bit) == 0)
                {
                    AddLine(batch, corners[index], corners[index | bit]);
                }
            }
        }
    }

    void DebugDraw3DService::Sphere(const Vec3& center, float radius, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        const Vec3 x{1.0f, 0.0f, 0.0f};
        const Vec3 y{0.0f, 1.0f, 0.0f};
        const Vec3 z{0.0f, 0.0f, 1.0f};
        AddCircle(batch, center, x, y, radius);
        AddCircle(batch, center, y, z, radius);
        AddCircle(batch, center, z, x, radius);
    }

    void DebugDraw3DService::Circle(const Vec3& center, const Vec3& normal, float radius, const Color& color, float duration,
        float thickness) const
    {
        Vec3 axisA;
        Vec3 axisB;
        Perpendiculars(normal, axisA, axisB);
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddCircle(batch, center, axisA, axisB, radius);
    }

    void DebugDraw3DService::Axes(const Vec3& position, const Quaternion& rotation, float size, float duration, float thickness) const
    {
        Line(position, Add(position, Scale(Rotate(rotation, Vec3{1.0f, 0.0f, 0.0f}), size)), Color{1.0f, 0.2f, 0.2f, 1.0f}, duration,
            thickness);
        Line(position, Add(position, Scale(Rotate(rotation, Vec3{0.0f, 1.0f, 0.0f}), size)), Color{0.2f, 1.0f, 0.2f, 1.0f}, duration,
            thickness);
        Line(position, Add(position, Scale(Rotate(rotation, Vec3{0.0f, 0.0f, 1.0f}), size)), Color{0.3f, 0.5f, 1.0f, 1.0f}, duration,
            thickness);
    }

    void DebugDraw3DService::Cross(const Vec3& at, float size, const Color& color, float duration, float thickness) const
    {
        Internal::DebugLineBatch batch(color, duration, thickness);
        AddLine(batch, Vec3{at.x - size, at.y, at.z}, Vec3{at.x + size, at.y, at.z});
        AddLine(batch, Vec3{at.x, at.y - size, at.z}, Vec3{at.x, at.y + size, at.z});
        AddLine(batch, Vec3{at.x, at.y, at.z - size}, Vec3{at.x, at.y, at.z + size});
    }
}
