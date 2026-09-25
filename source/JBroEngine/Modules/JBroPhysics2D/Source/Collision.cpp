#include <JBro/Physics2D/Collision.h>

#include "VectorMath.h"

#include <cfloat>
#include <cmath>

namespace JBro::Physics2D
{
    using namespace Internal;

    namespace
    {
        // 월드로 옮긴 볼록 조각. 법선은 반시계 변의 오른쪽, 즉 바깥이다.
        struct WorldPolygon
        {
            Vec2          points[MaxPolygonVertices];
            Vec2          normals[MaxPolygonVertices];
            std::uint32_t count = 0;
        };

        WorldPolygon ToWorld(const ConvexPolygon& polygon, const Pose& pose)
        {
            WorldPolygon world;
            world.count = polygon.count;
            for (std::uint32_t i = 0; i < polygon.count; ++i)
            {
                world.points[i] = TransformPoint(pose, polygon.points[i]);
            }
            for (std::uint32_t i = 0; i < polygon.count; ++i)
            {
                const Vec2 edge = Subtract(world.points[(i + 1) % world.count], world.points[i]);
                const float length = Length(edge);
                world.normals[i] = length > 0.0f ? Vec2{ edge.y / length, -edge.x / length } : Vec2{};
            }
            return world;
        }

        // a 의 변마다 b 가 그 변 바깥으로 얼마나 떨어져 있는지 재고 가장 큰 것을 돌려준다(SAT).
        float FindMaxSeparation(const WorldPolygon& a, const WorldPolygon& b, std::uint32_t& edge)
        {
            float best = -FLT_MAX;
            edge = 0;
            for (std::uint32_t i = 0; i < a.count; ++i)
            {
                float deepest = FLT_MAX;
                for (std::uint32_t j = 0; j < b.count; ++j)
                {
                    deepest = std::fmin(deepest, Dot(a.normals[i], Subtract(b.points[j], a.points[i])));
                }
                if (deepest > best)
                {
                    best = deepest;
                    edge = i;
                }
            }
            return best;
        }

        constexpr std::uint32_t InvalidEdge = 0xFFFFFFFFu;

        struct ClipVertex
        {
            Vec2          point;
            std::uint32_t id = 0;
        };

        // 평면 dot(normal, p) <= offset 쪽만 남긴다. 선분이 평면을 가로지르면 교점을 만든다.
        std::uint32_t ClipSegment(
            ClipVertex out[2], const ClipVertex in[2], Vec2 normal, float offset, std::uint32_t crossingId)
        {
            std::uint32_t count = 0;
            const float d0 = Dot(normal, in[0].point) - offset;
            const float d1 = Dot(normal, in[1].point) - offset;
            if (d0 <= 0.0f)
            {
                out[count] = in[0];
                ++count;
            }
            if (d1 <= 0.0f)
            {
                out[count] = in[1];
                ++count;
            }
            if (d0 * d1 < 0.0f)
            {
                const float t = d0 / (d0 - d1);
                out[count].point = Add(in[0].point, Scale(Subtract(in[1].point, in[0].point), t));
                out[count].id = crossingId;
                ++count;
            }
            return count;
        }
    }

    Vec2 RotateVector(Rotation rotation, Vec2 local)
    {
        return { rotation.c * local.x - rotation.s * local.y, rotation.s * local.x + rotation.c * local.y };
    }

    Vec2 TransformPoint(const Pose& pose, Vec2 local)
    {
        return Add(RotateVector(pose.rotation, local), pose.position);
    }

    Manifold CollideCircles(const Circle& a, const Pose& poseA, const Circle& b, const Pose& poseB)
    {
        Manifold manifold;
        const Vec2 centerA = TransformPoint(poseA, a.center);
        const Vec2 centerB = TransformPoint(poseB, b.center);
        const Vec2 delta = Subtract(centerB, centerA);
        const float distance = Length(delta);
        const float separation = distance - a.radius - b.radius;
        if (separation > SpeculativeDistance)
        {
            return manifold;
        }

        // 중심이 겹치면 방향이 없다. 아무 방향이든 일관되면 되므로 위로 민다.
        manifold.normal = distance > FLT_EPSILON ? Scale(delta, 1.0f / distance) : Vec2{ 0.0f, 1.0f };
        manifold.points[0].point = Add(centerA, Scale(manifold.normal, a.radius + 0.5f * separation));
        manifold.points[0].separation = separation;
        manifold.points[0].id = 0;
        manifold.count = 1;
        return manifold;
    }

    Manifold CollidePolygonAndCircle(
        const ConvexPolygon& a, const Pose& poseA, const Circle& b, const Pose& poseB)
    {
        Manifold manifold;
        if (a.count < 3)
        {
            return manifold;
        }

        const WorldPolygon polygon = ToWorld(a, poseA);
        const Vec2 center = TransformPoint(poseB, b.center);
        const float radius = b.radius;

        float faceSeparation = -FLT_MAX;
        std::uint32_t face = 0;
        for (std::uint32_t i = 0; i < polygon.count; ++i)
        {
            const float s = Dot(polygon.normals[i], Subtract(center, polygon.points[i]));
            if (s > radius + SpeculativeDistance)
            {
                return manifold;
            }
            if (s > faceSeparation)
            {
                faceSeparation = s;
                face = i;
            }
        }

        const Vec2 v1 = polygon.points[face];
        const Vec2 v2 = polygon.points[(face + 1) % polygon.count];
        Vec2 normal = polygon.normals[face];
        // 원 중심에서 도형 표면까지, 법선을 따라 잰 거리. 중심이 안에 있으면 음수다.
        float surfaceDistance = faceSeparation;
        std::uint32_t id = face;

        if (faceSeparation > FLT_EPSILON)
        {
            // 중심이 바깥이면 가장 가까운 것이 변인지 꼭짓점인지 가른다(보로노이 영역).
            const float u1 = Dot(Subtract(center, v1), Subtract(v2, v1));
            const float u2 = Dot(Subtract(center, v2), Subtract(v1, v2));
            if (u1 <= 0.0f || u2 <= 0.0f)
            {
                const Vec2 vertex = u1 <= 0.0f ? v1 : v2;
                const Vec2 delta = Subtract(center, vertex);
                const float distance = Length(delta);
                if (distance - radius > SpeculativeDistance)
                {
                    return manifold;
                }
                if (distance > FLT_EPSILON)
                {
                    normal = Scale(delta, 1.0f / distance);
                }
                surfaceDistance = distance;
                id = u1 <= 0.0f ? face : (face + 1) % polygon.count;
                id |= 0x100u;
            }
        }

        const float separation = surfaceDistance - radius;
        manifold.normal = normal;
        manifold.points[0].point = Subtract(center, Scale(normal, 0.5f * (radius + surfaceDistance)));
        manifold.points[0].separation = separation;
        manifold.points[0].id = id;
        manifold.count = 1;
        return manifold;
    }

    Manifold CollidePolygons(
        const ConvexPolygon& a, const Pose& poseA, const ConvexPolygon& b, const Pose& poseB)
    {
        Manifold manifold;
        if (a.count < 3 || b.count < 3)
        {
            return manifold;
        }

        const WorldPolygon worldA = ToWorld(a, poseA);
        const WorldPolygon worldB = ToWorld(b, poseB);

        std::uint32_t edgeA = 0;
        const float separationA = FindMaxSeparation(worldA, worldB, edgeA);
        if (separationA > SpeculativeDistance)
        {
            return manifold;
        }
        std::uint32_t edgeB = 0;
        const float separationB = FindMaxSeparation(worldB, worldA, edgeB);
        if (separationB > SpeculativeDistance)
        {
            return manifold;
        }

        // 기준면은 덜 박힌(더 떨어진) 쪽 도형의 변이다. 거의 같으면 A 를 고른다 - 매 스텝 기준이 바뀌면
        // 접촉 번호가 바뀌어 워밍스타트가 끊긴다.
        const WorldPolygon* reference = &worldA;
        const WorldPolygon* incident = &worldB;
        std::uint32_t referenceEdge = edgeA;
        bool flip = false;
        if (separationB > separationA + 0.1f * LinearSlop)
        {
            reference = &worldB;
            incident = &worldA;
            referenceEdge = edgeB;
            flip = true;
        }

        const Vec2 referenceNormal = reference->normals[referenceEdge];

        // 입사면은 상대 도형에서 기준 법선과 가장 반대로 향한 변이다.
        std::uint32_t incidentEdge = 0;
        float lowest = FLT_MAX;
        for (std::uint32_t i = 0; i < incident->count; ++i)
        {
            const float d = Dot(referenceNormal, incident->normals[i]);
            if (d < lowest)
            {
                lowest = d;
                incidentEdge = i;
            }
        }

        const Vec2 v11 = reference->points[referenceEdge];
        const Vec2 v12 = reference->points[(referenceEdge + 1) % reference->count];
        const Vec2 edge = Subtract(v12, v11);
        const float edgeLength = Length(edge);
        if (edgeLength <= 0.0f)
        {
            return manifold;
        }
        const Vec2 tangent = Scale(edge, 1.0f / edgeLength);

        // 번호는 (기준이 누구인가, 기준 변, 입사 변, 그 안의 자리) 다. 같은 두 면이 계속 맞닿아 있으면 같은 번호다.
        const std::uint32_t base = (flip ? 0x8000u : 0u) | (referenceEdge << 8) | (incidentEdge << 4);
        const ClipVertex incidentSegment[2] = {
            { incident->points[incidentEdge], base | 0u },
            { incident->points[(incidentEdge + 1) % incident->count], base | 1u } };

        ClipVertex clipped1[2];
        if (ClipSegment(clipped1, incidentSegment, Scale(tangent, -1.0f), -Dot(tangent, v11), base | 2u) < 2)
        {
            return manifold;
        }
        ClipVertex clipped2[2];
        if (ClipSegment(clipped2, clipped1, tangent, Dot(tangent, v12), base | 3u) < 2)
        {
            return manifold;
        }

        manifold.normal = flip ? Scale(referenceNormal, -1.0f) : referenceNormal;
        for (const ClipVertex& vertex : clipped2)
        {
            const float separation = Dot(referenceNormal, Subtract(vertex.point, v11));
            if (separation > SpeculativeDistance)
            {
                continue;
            }
            ManifoldPoint& point = manifold.points[manifold.count];
            // 입사 도형의 점에서 기준면까지의 가운데.
            point.point = Subtract(vertex.point, Scale(referenceNormal, 0.5f * separation));
            point.separation = separation;
            point.id = vertex.id;
            ++manifold.count;
        }
        return manifold;
    }

    Rect ComputePolygonBounds(const ConvexPolygon& polygon, const Pose& pose)
    {
        Rect bounds;
        if (polygon.count == 0)
        {
            bounds.min = pose.position;
            bounds.max = pose.position;
            return bounds;
        }
        bounds.min = TransformPoint(pose, polygon.points[0]);
        bounds.max = bounds.min;
        for (std::uint32_t i = 1; i < polygon.count; ++i)
        {
            const Vec2 point = TransformPoint(pose, polygon.points[i]);
            bounds.min = { std::fmin(bounds.min.x, point.x), std::fmin(bounds.min.y, point.y) };
            bounds.max = { std::fmax(bounds.max.x, point.x), std::fmax(bounds.max.y, point.y) };
        }
        return bounds;
    }

    bool RaycastPolygon(const ConvexPolygon& polygon, const Pose& pose,
        Vec2 origin, Vec2 direction, float maxDistance, float& distance, Vec2& normal)
    {
        if (polygon.count < 3 || maxDistance < 0.0f)
        {
            return false;
        }
        const WorldPolygon world = ToWorld(polygon, pose);

        // 변마다 반평면을 자른다(Cyrus-Beck). 들어가는 변 중 가장 늦은 것이 맞은 면이다.
        float lower = 0.0f;
        float upper = maxDistance;
        std::uint32_t entered = InvalidEdge;
        for (std::uint32_t i = 0; i < world.count; ++i)
        {
            const float numerator = Dot(world.normals[i], Subtract(world.points[i], origin));
            const float denominator = Dot(world.normals[i], direction);
            if (denominator == 0.0f)
            {
                if (numerator < 0.0f)
                {
                    return false;
                }
                continue;
            }
            const float t = numerator / denominator;
            if (denominator < 0.0f && t > lower)
            {
                lower = t;
                entered = i;
            }
            else if (denominator > 0.0f && t < upper)
            {
                upper = t;
            }
            if (upper < lower)
            {
                return false;
            }
        }

        if (entered == InvalidEdge)
        {
            distance = 0.0f;
            normal = Scale(direction, -1.0f);
            return true;
        }
        distance = lower;
        normal = world.normals[entered];
        return true;
    }

    bool RaycastCircle(const Circle& circle, const Pose& pose,
        Vec2 origin, Vec2 direction, float maxDistance, float& distance, Vec2& normal)
    {
        if (circle.radius <= 0.0f || maxDistance < 0.0f)
        {
            return false;
        }
        const Vec2 center = TransformPoint(pose, circle.center);
        const Vec2 offset = Subtract(origin, center);
        const float c = Dot(offset, offset) - circle.radius * circle.radius;
        if (c <= 0.0f)
        {
            distance = 0.0f;
            normal = Scale(direction, -1.0f);
            return true;
        }
        const float b = Dot(offset, direction);
        const float discriminant = b * b - c;
        if (b > 0.0f || discriminant < 0.0f)
        {
            return false;
        }
        const float t = -b - std::sqrt(discriminant);
        if (t > maxDistance)
        {
            return false;
        }
        distance = t;
        const Vec2 hit = Add(origin, Scale(direction, t));
        normal = Scale(Subtract(hit, center), 1.0f / circle.radius);
        return true;
    }

    bool OverlapPolygons(const ConvexPolygon& a, const Pose& poseA, const ConvexPolygon& b, const Pose& poseB)
    {
        if (a.count < 3 || b.count < 3)
        {
            return false;
        }
        const WorldPolygon worldA = ToWorld(a, poseA);
        const WorldPolygon worldB = ToWorld(b, poseB);
        std::uint32_t edge = 0;
        if (FindMaxSeparation(worldA, worldB, edge) > 0.0f)
        {
            return false;
        }
        return FindMaxSeparation(worldB, worldA, edge) <= 0.0f;
    }

    bool OverlapPolygonAndCircle(const ConvexPolygon& a, const Pose& poseA, const Circle& b, const Pose& poseB)
    {
        // 닿은 것은 표면 사이가 0 이하라는 뜻이다. 미리 만드는 접촉과 같은 판정을 쓰고 거리만 0 으로 본다.
        const Manifold manifold = CollidePolygonAndCircle(a, poseA, b, poseB);
        return manifold.count > 0 && manifold.points[0].separation <= 0.0f;
    }

    Rect ComputeCircleBounds(const Circle& circle, const Pose& pose)
    {
        const Vec2 center = TransformPoint(pose, circle.center);
        return { { center.x - circle.radius, center.y - circle.radius },
                 { center.x + circle.radius, center.y + circle.radius } };
    }
}
