#include <JBro/Physics2D/Collision.h>

#include "VectorMath.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Physics2D
{
    using namespace Internal;

    namespace
    {
        // 월드로 옮긴 볼록 조각. 법선은 반시계 변의 오른쪽, 즉 바깥이다.
        struct WorldPolygon
        {
            Vector2          points[MaxPolygonVertices];
            Vector2          normals[MaxPolygonVertices];
            UInt32 count = 0;
        };

        WorldPolygon ToWorld(const ConvexPolygon& polygon, const Pose& pose)
        {
            WorldPolygon world;
            world.count = polygon.count;
            for (UInt32 i = 0; i < polygon.count; ++i)
            {
                world.points[i] = TransformPoint(pose, polygon.points[i]);
            }
            for (UInt32 i = 0; i < polygon.count; ++i)
            {
                const Vector2 edge = Subtract(world.points[(i + 1) % world.count], world.points[i]);
                const Float length = Length(edge);
                world.normals[i] = length > 0.0f ? Vector2{ edge.y / length, -edge.x / length } : Vector2{};
            }
            return world;
        }

        // a 의 변마다 b 가 그 변 바깥으로 얼마나 떨어져 있는지 재고 가장 큰 것을 돌려준다(SAT).
        Float FindMaxSeparation(const WorldPolygon& a, const WorldPolygon& b, UInt32& edge)
        {
            Float best = -FLT_MAX;
            edge = 0;
            for (UInt32 i = 0; i < a.count; ++i)
            {
                Float deepest = FLT_MAX;
                for (UInt32 j = 0; j < b.count; ++j)
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

        constexpr UInt32 InvalidEdge = 0xFFFFFFFFu;

        struct ClipVertex
        {
            Vector2          point;
            UInt32 id = 0;
        };

        // 평면 dot(normal, p) <= offset 쪽만 남긴다. 선분이 평면을 가로지르면 교점을 만든다.
        UInt32 ClipSegment(
            ClipVertex out[2], const ClipVertex in[2], Vector2 normal, Float offset, UInt32 crossingId)
        {
            UInt32 count = 0;
            const Float d0 = Dot(normal, in[0].point) - offset;
            const Float d1 = Dot(normal, in[1].point) - offset;
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
                const Float t = d0 / (d0 - d1);
                out[count].point = Add(in[0].point, Scale(Subtract(in[1].point, in[0].point), t));
                out[count].id = crossingId;
                ++count;
            }
            return count;
        }

        // 두 선분 p1-q1, p2-q2 의 가장 가까운 두 점(Ericson, Real-Time Collision Detection 5.1.9). fraction 은 선분 위의 자리
        // (0 = p, 1 = q)다. 둥근 도형이 떨어져 있을 때 가장 가까운 것이 모서리끼리인지 가르는 데 쓴다.
        struct SegmentDistance
        {
            Float fraction1 = 0.0f;
            Float fraction2 = 0.0f;
            Vector2  closest1;
            Vector2  closest2;
            Float distanceSquared = 0.0f;
        };

        SegmentDistance ComputeSegmentDistance(Vector2 p1, Vector2 q1, Vector2 p2, Vector2 q2)
        {
            SegmentDistance result;
            const Vector2 d1 = Subtract(q1, p1);
            const Vector2 d2 = Subtract(q2, p2);
            const Vector2 r = Subtract(p1, p2);
            const Float dd1 = Dot(d1, d1);
            const Float dd2 = Dot(d2, d2);
            const Float rd1 = Dot(r, d1);
            const Float rd2 = Dot(r, d2);
            const Float tiny = FLT_EPSILON * FLT_EPSILON;
            if (dd1 < tiny || dd2 < tiny)
            {
                if (dd1 >= tiny)
                {
                    result.fraction1 = JBro::Clamp(-rd1 / dd1, 0.0f, 1.0f);
                }
                else if (dd2 >= tiny)
                {
                    result.fraction2 = JBro::Clamp(rd2 / dd2, 0.0f, 1.0f);
                }
            }
            else
            {
                const Float d12 = Dot(d1, d2);
                const Float denominator = dd1 * dd2 - d12 * d12;
                // 나란하면 첫 선분의 아무 점이나 된다. 끝점에서 시작해 둘째 선분에서 되짚는다.
                Float f1 = denominator != 0.0f ? JBro::Clamp((d12 * rd2 - rd1 * dd2) / denominator, 0.0f, 1.0f) : Float(0.0f);
                Float f2 = (d12 * f1 + rd2) / dd2;
                if (f2 < 0.0f)
                {
                    f2 = 0.0f;
                    f1 = JBro::Clamp(-rd1 / dd1, 0.0f, 1.0f);
                }
                else if (f2 > 1.0f)
                {
                    f2 = 1.0f;
                    f1 = JBro::Clamp((d12 - rd1) / dd1, 0.0f, 1.0f);
                }
                result.fraction1 = f1;
                result.fraction2 = f2;
            }
            result.closest1 = Add(p1, Scale(d1, result.fraction1));
            result.closest2 = Add(p2, Scale(d2, result.fraction2));
            result.distanceSquared = LengthSquared(Subtract(result.closest2, result.closest1));
            return result;
        }

        Bool IsEndpoint(Float fraction)
        {
            return fraction == 0.0f || fraction == 1.0f;
        }
    }

    namespace
    {
        Bool RaySegment(Vector2 origin, Vector2 direction, Vector2 a, Vector2 b, Float& t);
    }

    Vector2 RotateVector(Rotation rotation, Vector2 local)
    {
        return { rotation.c * local.x - rotation.s * local.y, rotation.s * local.x + rotation.c * local.y };
    }

    Vector2 TransformPoint(const Pose& pose, Vector2 local)
    {
        return Add(RotateVector(pose.rotation, local), pose.position);
    }

    Manifold CollideCircles(const Circle& a, const Pose& poseA, const Circle& b, const Pose& poseB)
    {
        Manifold manifold;
        const Vector2 centerA = TransformPoint(poseA, a.center);
        const Vector2 centerB = TransformPoint(poseB, b.center);
        const Vector2 delta = Subtract(centerB, centerA);
        const Float distance = Length(delta);
        const Float separation = distance - a.radius - b.radius;
        if (separation > SpeculativeDistance)
        {
            return manifold;
        }

        // 중심이 겹치면 방향이 없다. 아무 방향이든 일관되면 되므로 위로 민다.
        manifold.normal = distance > FLT_EPSILON ? Scale(delta, 1.0f / distance) : Vector2{ 0.0f, 1.0f };
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
        if (a.count < 2)
        {
            return manifold;
        }

        const WorldPolygon polygon = ToWorld(a, poseA);
        const Vector2 center = TransformPoint(poseB, b.center);
        // 둥근 폴리곤(캡슐)의 두께는 원의 반지름에 더해 재면 된다. 가운데 점만 두 표면 사이로 다시 잡는다.
        const Float radius = b.radius + a.radius;

        Float faceSeparation = -FLT_MAX;
        UInt32 face = 0;
        for (UInt32 i = 0; i < polygon.count; ++i)
        {
            const Float s = Dot(polygon.normals[i], Subtract(center, polygon.points[i]));
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

        const Vector2 v1 = polygon.points[face];
        const Vector2 v2 = polygon.points[(face + 1) % polygon.count];
        Vector2 normal = polygon.normals[face];
        // 원 중심에서 도형 표면까지, 법선을 따라 잰 거리. 중심이 안에 있으면 음수다.
        Float surfaceDistance = faceSeparation;
        UInt32 id = face;

        // 선분(캡슐의 코어)은 안이 없다. 중심이 선분을 늘인 줄 위에 있어도 끝 너머면 끝 점이 가장 가깝다.
        if (faceSeparation > FLT_EPSILON || a.count < 3)
        {
            // 중심이 바깥이면 가장 가까운 것이 변인지 꼭짓점인지 가른다(보로노이 영역).
            const Float u1 = Dot(Subtract(center, v1), Subtract(v2, v1));
            const Float u2 = Dot(Subtract(center, v2), Subtract(v1, v2));
            if (u1 <= 0.0f || u2 <= 0.0f)
            {
                const Vector2 vertex = u1 <= 0.0f ? v1 : v2;
                const Vector2 delta = Subtract(center, vertex);
                const Float distance = Length(delta);
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

        const Float separation = surfaceDistance - radius;
        manifold.normal = normal;
        manifold.points[0].point = Subtract(center, Scale(normal, 0.5f * (surfaceDistance - a.radius + b.radius)));
        manifold.points[0].separation = separation;
        manifold.points[0].id = id;
        manifold.count = 1;
        return manifold;
    }

    namespace
    {
        // 두 선분(캡슐 코어)끼리. 선분에는 옆 법선뿐이라 같은 줄 위에서 끝끼리 다가오는 둘을 SAT 가 가르지 못한다 -
        // 가장 가까운 두 점으로 잰다. 나란히 겹쳐 누우면 B 를 A 의 범위로 잘라 두 점을 만든다(쌓인 캡슐이 구르지 않게).
        Manifold CollideSegments(Vector2 p1, Vector2 q1, Float radiusA, Vector2 p2, Vector2 q2, Float radiusB)
        {
            Manifold manifold;
            const Float radius = radiusA + radiusB;
            const SegmentDistance closest = ComputeSegmentDistance(p1, q1, p2, q2);
            const Float reach = radius + SpeculativeDistance;
            if (closest.distanceSquared > reach * reach)
            {
                return manifold;
            }
            const Float distance = std::sqrt(closest.distanceSquared);
            const Vector2 d1 = Subtract(q1, p1);
            const Float length1 = Length(d1);

            if (length1 > FLT_EPSILON)
            {
                const Vector2 u1 = Scale(d1, 1.0f / length1);
                const Float fp2 = Dot(Subtract(p2, p1), u1);
                const Float fq2 = Dot(Subtract(q2, p1), u1);
                const Bool beyondA = (fp2 <= 0.0f && fq2 <= 0.0f) || (fp2 >= length1 && fq2 >= length1);
                if (false == beyondA)
                {
                    // B 의 두 끝을 A 의 [0, length1] 로 자른다.
                    Vector2 lower = fp2 < fq2 ? p2 : q2;
                    Vector2 upper = fp2 < fq2 ? q2 : p2;
                    const Float fLower = std::fmin(fp2, fq2);
                    const Float fUpper = std::fmax(fp2, fq2);
                    if (fLower < 0.0f && fUpper - fLower > FLT_EPSILON)
                    {
                        lower = Add(lower, Scale(Subtract(upper, lower), (0.0f - fLower) / (fUpper - fLower)));
                    }
                    if (fUpper > length1 && fUpper - fLower > FLT_EPSILON)
                    {
                        upper = Add(upper, Scale(Subtract(lower, upper), (fUpper - length1) / (fUpper - fLower)));
                    }
                    const Vector2 side{ -u1.y, u1.x };
                    const Float sideLower = Dot(Subtract(lower, p1), side);
                    const Float sideUpper = Dot(Subtract(upper, p1), side);
                    // 두 점이 A 의 같은 쪽에 있을 때만 면 접촉이다. 가로지르면 아래의 가장 가까운 점 하나로 간다.
                    if (sideLower * sideUpper > 0.0f)
                    {
                        const Vector2 normal = sideLower > 0.0f ? side : Scale(side, -1.0f);
                        const Vector2 clipped[2] = { lower, upper };
                        const Float gaps[2] = { std::fabs(sideLower), std::fabs(sideUpper) };
                        manifold.normal = normal;
                        for (UInt32 i = 0; i < 2; ++i)
                        {
                            const Float separation = gaps[i] - radius;
                            if (separation > SpeculativeDistance)
                            {
                                continue;
                            }
                            ManifoldPoint& point = manifold.points[manifold.count];
                            point.point = Subtract(clipped[i], Scale(normal, 0.5f * (gaps[i] - radiusA + radiusB)));
                            point.separation = separation;
                            point.id = i;
                            ++manifold.count;
                        }
                        return manifold;
                    }
                }
            }

            // 끝끼리이거나 가로지른다. 가장 가까운 두 점 사이로 한 점이다.
            Vector2 normal;
            if (distance > FLT_EPSILON)
            {
                normal = Scale(Subtract(closest.closest2, closest.closest1), 1.0f / distance);
            }
            else if (length1 > FLT_EPSILON)
            {
                normal = { -d1.y / length1, d1.x / length1 };
            }
            else
            {
                normal = { 0.0f, 1.0f };
            }
            manifold.normal = normal;
            manifold.points[0].point = Add(closest.closest1, Scale(normal, 0.5f * (distance + radiusA - radiusB)));
            manifold.points[0].separation = distance - radius;
            manifold.points[0].id = 2;
            manifold.count = 1;
            return manifold;
        }
    }

    Manifold CollidePolygons(
        const ConvexPolygon& a, const Pose& poseA, const ConvexPolygon& b, const Pose& poseB)
    {
        if (a.count == 2 && b.count == 2)
        {
            return CollideSegments(TransformPoint(poseA, a.points[0]), TransformPoint(poseA, a.points[1]), a.radius,
                TransformPoint(poseB, b.points[0]), TransformPoint(poseB, b.points[1]), b.radius);
        }
        Manifold manifold;
        if (a.count < 2 || b.count < 2)
        {
            return manifold;
        }

        const WorldPolygon worldA = ToWorld(a, poseA);
        const WorldPolygon worldB = ToWorld(b, poseB);
        // 두께(캡슐의 반지름)는 코어끼리 잰 뒤 뺀다.
        const Float radius = a.radius + b.radius;

        UInt32 edgeA = 0;
        const Float separationA = FindMaxSeparation(worldA, worldB, edgeA);
        if (separationA > SpeculativeDistance + radius)
        {
            return manifold;
        }
        UInt32 edgeB = 0;
        const Float separationB = FindMaxSeparation(worldB, worldA, edgeB);
        if (separationB > SpeculativeDistance + radius)
        {
            return manifold;
        }

        // 기준면은 덜 박힌(더 떨어진) 쪽 도형의 변이다. 거의 같으면 A 를 고른다 - 매 스텝 기준이 바뀌면
        // 접촉 번호가 바뀌어 워밍스타트가 끊긴다.
        const WorldPolygon* reference = &worldA;
        const WorldPolygon* incident = &worldB;
        UInt32 referenceEdge = edgeA;
        Float referenceRadius = a.radius;
        Float incidentRadius = b.radius;
        Bool flip = false;
        if (separationB > separationA + 0.1f * LinearSlop)
        {
            reference = &worldB;
            incident = &worldA;
            referenceEdge = edgeB;
            referenceRadius = b.radius;
            incidentRadius = a.radius;
            flip = true;
        }

        const Vector2 referenceNormal = reference->normals[referenceEdge];

        // 입사면은 상대 도형에서 기준 법선과 가장 반대로 향한 변이다.
        UInt32 incidentEdge = 0;
        Float lowest = FLT_MAX;
        for (UInt32 i = 0; i < incident->count; ++i)
        {
            const Float d = Dot(referenceNormal, incident->normals[i]);
            if (d < lowest)
            {
                lowest = d;
                incidentEdge = i;
            }
        }

        const Vector2 v11 = reference->points[referenceEdge];
        const Vector2 v12 = reference->points[(referenceEdge + 1) % reference->count];
        const Vector2 v21 = incident->points[incidentEdge];
        const Vector2 v22 = incident->points[(incidentEdge + 1) % incident->count];

        // 둥근 도형의 코어가 떨어져 있으면 가장 가까운 것이 모서리끼리일 수 있다. 면 법선만으로는 둥근 모서리를 돌아가는
        // 방향이 나오지 않아(모서리 옆의 틈을 닿은 것으로 본다) 두 모서리를 잇는 방향으로 한 점을 만든다(Box2D v3 와 같다).
        // 날카로운 도형은 지금까지의 판정을 그대로 쓴다.
        const Float coreSeparation = flip ? separationB : separationA;
        if (radius > 0.0f && coreSeparation > 0.1f * LinearSlop)
        {
            const SegmentDistance closest = ComputeSegmentDistance(v11, v12, v21, v22);
            const Float distance = std::sqrt(closest.distanceSquared);
            const Vector2 normal = distance > FLT_EPSILON
                ? Scale(Subtract(closest.closest2, closest.closest1), 1.0f / distance)
                : referenceNormal;
            // 모서리가 기준면 바로 앞이면(두 모서리를 잇는 방향이 기준 법선이면) 면 판정이 같은 거리를 주고 두 점을 지킨다 -
            // 상자 끝과 나란히 누운 캡슐이 한 점으로 서면 기운다.
            if (IsEndpoint(closest.fraction1) && IsEndpoint(closest.fraction2)
                && Dot(normal, referenceNormal) < 1.0f - 1.0e-4f)
            {
                if (distance - radius > SpeculativeDistance)
                {
                    return manifold;
                }
                manifold.normal = flip ? Scale(normal, -1.0f) : normal;
                ManifoldPoint& point = manifold.points[0];
                point.point = Add(closest.closest1, Scale(normal, 0.5f * (distance + referenceRadius - incidentRadius)));
                point.separation = distance - radius;
                // 모서리끼리의 번호. 면-면 번호(아래 네 비트가 0~3)와 겹치지 않게 아래 네 비트를 4 로 둔다.
                const UInt32 referenceVertex =
                    closest.fraction1 == 0.0f ? referenceEdge : (referenceEdge + 1) % reference->count;
                const UInt32 incidentVertex =
                    closest.fraction2 == 0.0f ? incidentEdge : (incidentEdge + 1) % incident->count;
                point.id = (flip ? 0x8000u : 0u) | (referenceVertex << 8) | (incidentVertex << 4) | 4u;
                manifold.count = 1;
                return manifold;
            }
        }

        const Vector2 edge = Subtract(v12, v11);
        const Float edgeLength = Length(edge);
        if (edgeLength <= 0.0f)
        {
            return manifold;
        }
        const Vector2 tangent = Scale(edge, 1.0f / edgeLength);

        // 번호는 (기준이 누구인가, 기준 변, 입사 변, 그 안의 자리) 다. 같은 두 면이 계속 맞닿아 있으면 같은 번호다.
        const UInt32 base = (flip ? 0x8000u : 0u) | (referenceEdge << 8) | (incidentEdge << 4);
        const ClipVertex incidentSegment[2] = {
            { v21, base | 0u },
            { v22, base | 1u } };

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
            const Float coreGap = Dot(referenceNormal, Subtract(vertex.point, v11));
            const Float separation = coreGap - radius;
            if (separation > SpeculativeDistance)
            {
                continue;
            }
            ManifoldPoint& point = manifold.points[manifold.count];
            // 입사 도형의 표면(코어 점에서 두께만큼 안쪽)과 기준 도형의 표면(기준면에서 두께만큼 바깥) 사이의 가운데.
            point.point = Subtract(vertex.point, Scale(referenceNormal, 0.5f * (coreGap - referenceRadius + incidentRadius)));
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
        for (UInt32 i = 1; i < polygon.count; ++i)
        {
            bounds = UnionRect(bounds, TransformPoint(pose, polygon.points[i]));
        }
        // 둥근 도형의 두께만큼 네 방향으로 넓힌다.
        return ExpandRect(bounds, polygon.radius);
    }

    Bool RaycastPolygon(const ConvexPolygon& polygon, const Pose& pose,
        Vector2 origin, Vector2 direction, Float maxDistance, Float& distance, Vector2& normal)
    {
        if (polygon.radius > 0.0f)
        {
            // 둥근 도형에 쏘는 반직선은 반지름 0 인 원을 그 코어에 미는 것과 같다(스윕이 두께를 더한다).
            return CastCircle(origin, 0.0f, direction, maxDistance, polygon, pose, distance, normal);
        }
        if (polygon.count == 2 && maxDistance >= 0.0f)
        {
            // 두께 없는 선분(체인)이다. 양면으로 맞고, 법선은 쏜 쪽을 향한다.
            const Vector2 a = TransformPoint(pose, polygon.points[0]);
            const Vector2 b = TransformPoint(pose, polygon.points[1]);
            Float t = 0.0f;
            if (false == RaySegment(origin, direction, a, b, t) || t > maxDistance)
            {
                return false;
            }
            const Vector2 edge = Subtract(b, a);
            const Float length = Length(edge);
            if (length <= 0.0f)
            {
                return false;
            }
            Vector2 n{ edge.y / length, -edge.x / length };
            if (Dot(n, direction) > 0.0f)
            {
                n = Scale(n, -1.0f);
            }
            distance = t;
            normal = n;
            return true;
        }
        if (polygon.count < 3 || maxDistance < 0.0f)
        {
            return false;
        }
        const WorldPolygon world = ToWorld(polygon, pose);

        // 변마다 반평면을 자른다(Cyrus-Beck). 들어가는 변 중 가장 늦은 것이 맞은 면이다.
        Float lower = 0.0f;
        Float upper = maxDistance;
        UInt32 entered = InvalidEdge;
        for (UInt32 i = 0; i < world.count; ++i)
        {
            const Float numerator = Dot(world.normals[i], Subtract(world.points[i], origin));
            const Float denominator = Dot(world.normals[i], direction);
            if (denominator == 0.0f)
            {
                if (numerator < 0.0f)
                {
                    return false;
                }
                continue;
            }
            const Float t = numerator / denominator;
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

    Bool RaycastCircle(const Circle& circle, const Pose& pose,
        Vector2 origin, Vector2 direction, Float maxDistance, Float& distance, Vector2& normal)
    {
        if (circle.radius <= 0.0f || maxDistance < 0.0f)
        {
            return false;
        }
        const Vector2 center = TransformPoint(pose, circle.center);
        const Vector2 offset = Subtract(origin, center);
        const Float c = Dot(offset, offset) - circle.radius * circle.radius;
        if (c <= 0.0f)
        {
            distance = 0.0f;
            normal = Scale(direction, -1.0f);
            return true;
        }
        const Float b = Dot(offset, direction);
        const Float discriminant = b * b - c;
        if (b > 0.0f || discriminant < 0.0f)
        {
            return false;
        }
        const Float t = -b - std::sqrt(discriminant);
        if (t > maxDistance)
        {
            return false;
        }
        distance = t;
        const Vector2 hit = Add(origin, Scale(direction, t));
        normal = Scale(Subtract(hit, center), 1.0f / circle.radius);
        return true;
    }

    Bool OverlapPolygons(const ConvexPolygon& a, const Pose& poseA, const ConvexPolygon& b, const Pose& poseB)
    {
        if (a.radius + b.radius > 0.0f || a.count == 2 || b.count == 2)
        {
            // 둥근 도형은 면 법선의 SAT 만으로 모서리 옆 틈을 가르지 못한다. 두께 없는 선분(체인)도 이 길로 잰다. 접촉 판정과 같은 매니폴드로 표면 사이를 잰다.
            const Manifold manifold = CollidePolygons(a, poseA, b, poseB);
            for (UInt32 i = 0; i < manifold.count; ++i)
            {
                if (manifold.points[i].separation <= 0.0f)
                {
                    return true;
                }
            }
            return false;
        }
        if (a.count < 3 || b.count < 3)
        {
            return false;
        }
        const WorldPolygon worldA = ToWorld(a, poseA);
        const WorldPolygon worldB = ToWorld(b, poseB);
        UInt32 edge = 0;
        if (FindMaxSeparation(worldA, worldB, edge) > 0.0f)
        {
            return false;
        }
        return FindMaxSeparation(worldB, worldA, edge) <= 0.0f;
    }

    Bool OverlapPolygonAndCircle(const ConvexPolygon& a, const Pose& poseA, const Circle& b, const Pose& poseB)
    {
        // 닿은 것은 표면 사이가 0 이하라는 뜻이다. 미리 만드는 접촉과 같은 판정을 쓰고 거리만 0 으로 본다.
        const Manifold manifold = CollidePolygonAndCircle(a, poseA, b, poseB);
        return manifold.count > 0 && manifold.points[0].separation <= 0.0f;
    }

    namespace
    {
        // 점 모음의 볼록 껍질(반시계, Andrew 의 단조 사슬). 일직선 점은 뺀다. `out` 은 `count + 1` 칸이면 된다.
        UInt32 ConvexHull(Vector2* points, UInt32 count, Vector2* out)
        {
            if (count < 3)
            {
                for (UInt32 i = 0; i < count; ++i)
                {
                    out[i] = points[i];
                }
                return count;
            }
            std::sort(points, points + count, [](Vector2 a, Vector2 b)
            {
                return a.x != b.x ? a.x < b.x : a.y < b.y;
            });
            UInt32 size = 0;
            for (UInt32 i = 0; i < count; ++i)
            {
                while (size >= 2 && Cross(Subtract(out[size - 1], out[size - 2]), Subtract(points[i], out[size - 2])) <= 0.0f)
                {
                    --size;
                }
                out[size] = points[i];
                ++size;
            }
            const UInt32 lower = size + 1;
            for (UInt32 i = count - 1; i > 0; --i)
            {
                const Vector2 point = points[i - 1];
                while (size >= lower && Cross(Subtract(out[size - 1], out[size - 2]), Subtract(point, out[size - 2])) <= 0.0f)
                {
                    --size;
                }
                out[size] = point;
                ++size;
            }
            return size - 1;
        }

        // 반시계 볼록 껍질에 원점에서 반직선을 쏜다(Cyrus-Beck). 원점이 안이면 거리 0 이다.
        Bool RaycastHull(const Vector2* hull, UInt32 count, Vector2 direction, Float maxDistance,
            Float& distance, Vector2& normal)
        {
            if (count < 3)
            {
                return false;
            }
            Float lower = 0.0f;
            Float upper = maxDistance;
            Bool entered = false;
            Vector2 enteredNormal;
            for (UInt32 i = 0; i < count; ++i)
            {
                const Vector2 a = hull[i];
                const Vector2 b = hull[(i + 1) % count];
                const Vector2 edge = Subtract(b, a);
                const Float length = Length(edge);
                if (length <= 0.0f)
                {
                    continue;
                }
                const Vector2 outward{ edge.y / length, -edge.x / length };
                const Float numerator = Dot(outward, a);
                const Float denominator = Dot(outward, direction);
                if (denominator == 0.0f)
                {
                    if (numerator < 0.0f)
                    {
                        return false;
                    }
                    continue;
                }
                const Float t = numerator / denominator;
                if (denominator < 0.0f && t > lower)
                {
                    lower = t;
                    entered = true;
                    enteredNormal = outward;
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
            distance = entered ? lower : Float(0.0f);
            normal = entered ? enteredNormal : Scale(direction, -1.0f);
            return true;
        }

        // 선분(반직선이 뒤에서 들어오지 않는 쪽)과 반직선. 맞으면 원점에서의 거리.
        Bool RaySegment(Vector2 origin, Vector2 direction, Vector2 a, Vector2 b, Float& t)
        {
            const Vector2 edge = Subtract(b, a);
            const Float denominator = Cross(direction, edge);
            if (denominator == 0.0f)
            {
                return false;
            }
            const Vector2 toA = Subtract(a, origin);
            const Float along = Cross(toA, edge) / denominator;
            const Float across = Cross(toA, direction) / denominator;
            if (along < 0.0f || across < 0.0f || across > 1.0f)
            {
                return false;
            }
            t = along;
            return true;
        }

        // 원점에서 반시계 볼록 점 모음(두 점이면 선분, 한 점이면 점)까지의 거리. 안이면 0 이다.
        Float DistanceToHull(const Vector2* hull, UInt32 count)
        {
            if (count == 1)
            {
                return Length(hull[0]);
            }
            Bool inside = count >= 3;
            Float best = FLT_MAX;
            for (UInt32 i = 0; i < count; ++i)
            {
                const Vector2 a = hull[i];
                const Vector2 b = hull[(i + 1) % count];
                const Vector2 edge = Subtract(b, a);
                if (Cross(edge, Scale(a, -1.0f)) < 0.0f)
                {
                    inside = false;
                }
                const Float lengthSquared = LengthSquared(edge);
                const Float along = lengthSquared > 0.0f ? JBro::Clamp(-Dot(a, edge) / lengthSquared, 0.0f, 1.0f) : Float(0.0f);
                best = std::fmin(best, Length(Add(a, Scale(edge, along))));
            }
            return inside ? Float(0.0f) : best;
        }

        // 반시계 볼록 점 모음을 radius 만큼 부풀린 모양에 원점에서 반직선을 쏜다: 앞면마다 민 선분, 꼭짓점마다 원.
        // 원점이 이미 안이면 거리 0 이다.
        Bool RaycastRoundedHull(const Vector2* hull, UInt32 count, Float radius, Vector2 direction, Float maxDistance,
            Float& distance, Vector2& normal)
        {
            if (count == 0)
            {
                return false;
            }
            if (DistanceToHull(hull, count) <= radius)
            {
                distance = 0.0f;
                normal = Scale(direction, -1.0f);
                return true;
            }
            const Vector2 origin{};
            Bool hit = false;
            Float best = maxDistance;
            Vector2 bestNormal;
            for (UInt32 i = 0; count >= 2 && i < count; ++i)
            {
                const Vector2 edge = Subtract(hull[(i + 1) % count], hull[i]);
                const Float length = Length(edge);
                if (length <= 0.0f)
                {
                    continue;
                }
                const Vector2 n{ edge.y / length, -edge.x / length };
                if (Dot(n, direction) >= 0.0f)
                {
                    continue;
                }
                Float t = 0.0f;
                if (RaySegment(origin, direction, Add(hull[i], Scale(n, radius)),
                        Add(hull[(i + 1) % count], Scale(n, radius)), t) && t <= best)
                {
                    hit = true;
                    best = t;
                    bestNormal = n;
                }
            }
            for (UInt32 i = 0; i < count; ++i)
            {
                Circle corner;
                corner.center = hull[i];
                corner.radius = radius;
                Float t = 0.0f;
                Vector2 n;
                if (RaycastCircle(corner, Pose{}, origin, direction, best, t, n) && t <= best)
                {
                    hit = true;
                    best = t;
                    bestNormal = n;
                }
            }
            if (hit)
            {
                distance = best;
                normal = bestNormal;
            }
            return hit;
        }
    }

    Bool ContainsPoint(const ConvexPolygon& polygon, const Pose& pose, Vector2 point)
    {
        if (polygon.radius > 0.0f)
        {
            // 코어에서 두께 안이면 안이다. 반지름 0 인 원과의 판정이 그 거리를 잰다.
            Circle probe;
            probe.center = point;
            const Manifold manifold = CollidePolygonAndCircle(polygon, pose, probe, Pose{});
            return manifold.count > 0 && manifold.points[0].separation <= 0.0f;
        }
        if (polygon.count < 3)
        {
            return false;
        }
        const WorldPolygon world = ToWorld(polygon, pose);
        for (UInt32 i = 0; i < world.count; ++i)
        {
            if (Dot(world.normals[i], Subtract(point, world.points[i])) > 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    Bool ContainsPoint(const Circle& circle, const Pose& pose, Vector2 point)
    {
        return LengthSquared(Subtract(point, TransformPoint(pose, circle.center))) <= circle.radius * circle.radius;
    }

    Bool OverlapCircles(const Circle& a, const Pose& poseA, const Circle& b, const Pose& poseB)
    {
        const Float reach = a.radius + b.radius;
        return LengthSquared(Subtract(TransformPoint(poseB, b.center), TransformPoint(poseA, a.center))) <= reach * reach;
    }

    Bool CastCircle(Vector2 center, Float radius, Vector2 direction, Float maxDistance,
        const ConvexPolygon& target, const Pose& targetPose, Float& distance, Vector2& normal)
    {
        if (target.count < 2 || (target.count < 3 && radius + target.radius <= 0.0f) || radius < 0.0f || maxDistance < 0.0f)
        {
            return false;
        }
        // 출발부터 닿아 있는가. 원-폴리곤 판정의 틈이 0 이하면 겹친 것이다.
        Circle probe;
        probe.center = center;
        probe.radius = radius;
        const Manifold start = CollidePolygonAndCircle(target, targetPose, probe, Pose{});
        if (start.count > 0 && start.points[0].separation <= 0.0f)
        {
            distance = 0.0f;
            normal = Scale(direction, -1.0f);
            return true;
        }

        // 폴리곤을 반지름만큼 부풀린 모양(민코프스키 합)에 원의 중심을 쏜다: 변마다 법선으로 민 선분, 꼭짓점마다 원.
        // 둥근 대상은 그 두께까지 부풀린다.
        const WorldPolygon world = ToWorld(target, targetPose);
        const Float grown = radius + target.radius;
        Bool hit = false;
        Float best = maxDistance;
        Vector2 bestNormal;
        for (UInt32 i = 0; i < world.count; ++i)
        {
            const Vector2 n = world.normals[i];
            if (Dot(n, direction) >= 0.0f)
            {
                continue;
            }
            const Vector2 a = Add(world.points[i], Scale(n, grown));
            const Vector2 b = Add(world.points[(i + 1) % world.count], Scale(n, grown));
            Float t = 0.0f;
            if (RaySegment(center, direction, a, b, t) && t <= best)
            {
                hit = true;
                best = t;
                bestNormal = n;
            }
        }
        for (UInt32 i = 0; i < world.count; ++i)
        {
            Circle corner;
            corner.center = world.points[i];
            corner.radius = grown;
            Float t = 0.0f;
            Vector2 n;
            if (grown > 0.0f && RaycastCircle(corner, Pose{}, center, direction, best, t, n) && t <= best)
            {
                hit = true;
                best = t;
                bestNormal = n;
            }
        }
        if (hit)
        {
            distance = best;
            normal = bestNormal;
        }
        return hit;
    }

    Bool CastCircle(Vector2 center, Float radius, Vector2 direction, Float maxDistance,
        const Circle& target, const Pose& targetPose, Float& distance, Vector2& normal)
    {
        // 두 원은 반지름을 더한 한 원에 중심을 쏘는 것과 같다.
        Circle grown = target;
        grown.radius = target.radius + radius;
        return RaycastCircle(grown, targetPose, center, direction, maxDistance, distance, normal);
    }

    Bool CastPolygon(const ConvexPolygon& moving, const Pose& start, Vector2 direction, Float maxDistance,
        const ConvexPolygon& target, const Pose& targetPose, Float& distance, Vector2& normal)
    {
        const Float grown = moving.radius + target.radius;
        if (moving.count < 2 || target.count < 2 || (grown <= 0.0f && moving.count < 3 && target.count < 3)
            || maxDistance < 0.0f)
        {
            return false;
        }
        // 미는 조각이 s 만큼 옮겨졌을 때 닿는 것은 s 가 (target - moving) 안에 들 때다. 그 껍질에 원점에서 쏜다.
        const WorldPolygon a = ToWorld(moving, start);
        const WorldPolygon b = ToWorld(target, targetPose);
        Vector2 differences[MaxPolygonVertices * MaxPolygonVertices];
        UInt32 count = 0;
        for (UInt32 i = 0; i < b.count; ++i)
        {
            for (UInt32 j = 0; j < a.count; ++j)
            {
                differences[count] = Subtract(b.points[i], a.points[j]);
                ++count;
            }
        }
        Vector2 hull[MaxPolygonVertices * MaxPolygonVertices + 1];
        const UInt32 hullCount = ConvexHull(differences, count, hull);
        if (grown > 0.0f)
        {
            // 두께가 있으면 그 껍질을 두께만큼 부풀린 모양에 쏜다.
            return RaycastRoundedHull(hull, hullCount, grown, direction, maxDistance, distance, normal);
        }
        return RaycastHull(hull, hullCount, direction, maxDistance, distance, normal);
    }

    Bool CastPolygon(const ConvexPolygon& moving, const Pose& start, Vector2 direction, Float maxDistance,
        const Circle& target, const Pose& targetPose, Float& distance, Vector2& normal)
    {
        // 조각이 원 쪽으로 가는 것은 원이 거꾸로 조각 쪽으로 오는 것과 같다. 원 스윕의 법선은 조각의 바깥이므로
        // 뒤집으면 원 표면에서 조각을 향한 법선이다.
        const Vector2 back = Scale(direction, -1.0f);
        if (false == CastCircle(TransformPoint(targetPose, target.center), target.radius, back, maxDistance,
                moving, start, distance, normal))
        {
            return false;
        }
        normal = distance == 0.0f ? back : Scale(normal, -1.0f);
        return true;
    }

    Rect ComputeCircleBounds(const Circle& circle, const Pose& pose)
    {
        const Vector2 center = TransformPoint(pose, circle.center);
        return { { center.x - circle.radius, center.y - circle.radius },
                 { center.x + circle.radius, center.y + circle.radius } };
    }

    namespace
    {
        enum class ChainVerdict
        {
            Keep,
            UseFace,
            Drop,
        };

        ChainSegment ToWorld(const ChainSegment& segment, const Pose& pose)
        {
            ChainSegment world = segment;
            world.p1 = TransformPoint(pose, segment.p1);
            world.p2 = TransformPoint(pose, segment.p2);
            world.previous = TransformPoint(pose, segment.previous);
            world.next = TransformPoint(pose, segment.next);
            return world;
        }

        ConvexPolygon SegmentPolygon(const ChainSegment& segment)
        {
            ConvexPolygon polygon;
            polygon.points[0] = segment.p1;
            polygon.points[1] = segment.p2;
            polygon.count = 2;
            return polygon;
        }

        // 선분(월드)과 만난 법선을 받을지, 면 법선으로 바꿀지, 버릴지 정한다. face 는 만난 쪽의 면 법선이다. 어느 쪽인지는 상대의
        // 중심으로 정한다 - 이음매의 유령 법선은 가로라 그 부호로는 위·아래를 가를 수 없다.
        ChainVerdict JudgeChainNormal(const ChainSegment& segment, Vector2 point, Vector2 normal, Vector2 otherCenter, Vector2& face)
        {
            const Vector2 edge = Subtract(segment.p2, segment.p1);
            const Float length = Length(edge);
            if (length <= 0.0f)
            {
                return ChainVerdict::Drop;
            }
            const Vector2 tangent = Scale(edge, 1.0f / length);
            const Vector2 segmentNormal{ tangent.y, -tangent.x };
            face = Dot(Subtract(otherCenter, segment.p1), segmentNormal) >= 0.0f ? segmentNormal : Scale(segmentNormal, -1.0f);
            if (Dot(normal, face) >= 1.0f - 1.0e-3f)
            {
                return ChainVerdict::Keep;
            }
            // 모서리 영역이다. 접촉점에 가까운 끝의 이웃을 본다. 이웃이 없는 끝은 진짜 모서리다.
            const Bool atEnd = Dot(Subtract(point, segment.p1), tangent) > 0.5f * length;
            if (false == (atEnd ? segment.hasNext : segment.hasPrevious))
            {
                return ChainVerdict::Keep;
            }
            const Vector2 vertex = atEnd ? segment.p2 : segment.p1;
            const Vector2 toGhost = Subtract(atEnd ? segment.next : segment.previous, vertex);
            const Float ghostLength = Length(toGhost);
            if (ghostLength <= 0.0f)
            {
                return ChainVerdict::UseFace;
            }
            // 이웃이 만난 쪽에서 멀어지면(내 쪽에서) 볼록한 꼭짓점이다. 아니면 평평하거나 오목해서 누구의 모서리도 아니다.
            if (Dot(toGhost, face) >= -LinearSlop * ghostLength)
            {
                return ChainVerdict::UseFace;
            }
            // 이웃도 자기가 만난 쪽에서 볼록하게 보면 둘 중 그 꼭짓점을 끝(p2)으로 가진 선분만 맡는다. 이웃이 오목하게 보면
            // 이웃은 면으로 보고 놓으니 내가 맡는다 - 양면 체인이라 두 선분이 고른 쪽이 다를 수 있다.
            const Vector2 ghostDirection = Scale(toGhost, 1.0f / ghostLength);
            Vector2 neighborFace{ ghostDirection.y, -ghostDirection.x };
            if (Dot(Subtract(otherCenter, vertex), neighborFace) < 0.0f)
            {
                neighborFace = Scale(neighborFace, -1.0f);
            }
            const Vector2 toMine = Subtract(atEnd ? segment.p1 : segment.p2, vertex);
            const Bool neighborConvex = Dot(toMine, neighborFace) < -LinearSlop * length;
            if (neighborConvex && false == atEnd)
            {
                return ChainVerdict::Drop;
            }
            // 맡은 꼭짓점에서는 상대의 법선을 그대로 받는다(다각형 면이 꼭짓점에 닿으면 내 면보다 기울어도 그 면 법선이 맞다).
            // 내 면도, 이웃의 바깥 법선(내 선분에서 멀어지는 쪽)도 등지면 쐐기 안으로 박힌 것이라 내 면으로 민다.
            Vector2 neighborOut{ ghostDirection.y, -ghostDirection.x };
            if (Dot(neighborOut, toMine) > 0.0f)
            {
                neighborOut = Scale(neighborOut, -1.0f);
            }
            if (Dot(normal, face) > 0.0f || Dot(normal, neighborOut) > 0.0f)
            {
                return ChainVerdict::Keep;
            }
            return ChainVerdict::UseFace;
        }
    }

    Manifold CollideChainSegmentAndPolygon(
        const ChainSegment& segment, const Pose& poseA, const ConvexPolygon& polygon, const Pose& poseB)
    {
        const Manifold raw = CollidePolygons(SegmentPolygon(segment), poseA, polygon, poseB);
        if (raw.count == 0)
        {
            return raw;
        }
        const ChainSegment world = ToWorld(segment, poseA);
        Vector2 centroid;
        for (UInt32 i = 0; i < polygon.count; ++i)
        {
            centroid = Add(centroid, polygon.points[i]);
        }
        centroid = TransformPoint(poseB, Scale(centroid, 1.0f / static_cast<JBro::Float>(polygon.count)));
        Vector2 face;
        const ChainVerdict verdict = JudgeChainNormal(world, raw.points[0].point, raw.normal, centroid, face);
        if (verdict == ChainVerdict::Keep)
        {
            return raw;
        }
        Manifold manifold;
        if (verdict == ChainVerdict::Drop)
        {
            return manifold;
        }
        // 선분의 면을 기준면으로 삼아 상대의 입사면을 선분 범위로 자른다.
        const WorldPolygon other = ToWorld(polygon, poseB);
        UInt32 incidentEdge = 0;
        Float lowest = FLT_MAX;
        for (UInt32 i = 0; i < other.count; ++i)
        {
            const Float d = Dot(face, other.normals[i]);
            if (d < lowest)
            {
                lowest = d;
                incidentEdge = i;
            }
        }
        const Vector2 edge = Subtract(world.p2, world.p1);
        const Vector2 tangent = Scale(edge, 1.0f / Length(edge));
        const UInt32 base = 0x4000u | (incidentEdge << 4);
        const ClipVertex incident[2] = {
            { other.points[incidentEdge], base | 0u },
            { other.points[(incidentEdge + 1) % other.count], base | 1u } };
        ClipVertex clipped1[2];
        if (ClipSegment(clipped1, incident, Scale(tangent, -1.0f), -Dot(tangent, world.p1), base | 2u) < 2)
        {
            return manifold;
        }
        ClipVertex clipped2[2];
        if (ClipSegment(clipped2, clipped1, tangent, Dot(tangent, world.p2), base | 3u) < 2)
        {
            return manifold;
        }
        manifold.normal = face;
        for (const ClipVertex& vertex : clipped2)
        {
            const Float coreGap = Dot(face, Subtract(vertex.point, world.p1));
            const Float separation = coreGap - polygon.radius;
            if (separation > SpeculativeDistance)
            {
                continue;
            }
            ManifoldPoint& point = manifold.points[manifold.count];
            point.point = Subtract(vertex.point, Scale(face, 0.5f * (coreGap + polygon.radius)));
            point.separation = separation;
            point.id = vertex.id;
            ++manifold.count;
        }
        return manifold;
    }

    Manifold CollideChainSegmentAndCircle(
        const ChainSegment& segment, const Pose& poseA, const Circle& circle, const Pose& poseB)
    {
        const Manifold raw = CollidePolygonAndCircle(SegmentPolygon(segment), poseA, circle, poseB);
        if (raw.count == 0)
        {
            return raw;
        }
        const ChainSegment world = ToWorld(segment, poseA);
        Vector2 face;
        const ChainVerdict verdict =
            JudgeChainNormal(world, raw.points[0].point, raw.normal, TransformPoint(poseB, circle.center), face);
        if (verdict == ChainVerdict::Keep)
        {
            return raw;
        }
        Manifold manifold;
        if (verdict == ChainVerdict::Drop)
        {
            return manifold;
        }
        // 중심이 선분 범위 밖이면 이웃 선분의 면이 맡는다(그쪽에서는 면 영역이다).
        const Vector2 center = TransformPoint(poseB, circle.center);
        const Vector2 edge = Subtract(world.p2, world.p1);
        const Float length = Length(edge);
        const Vector2 tangent = Scale(edge, 1.0f / length);
        const Float along = Dot(Subtract(center, world.p1), tangent);
        if (along < 0.0f || along > length)
        {
            return manifold;
        }
        const Float gap = Dot(Subtract(center, world.p1), face);
        const Float separation = gap - circle.radius;
        if (separation > SpeculativeDistance)
        {
            return manifold;
        }
        manifold.normal = face;
        manifold.points[0].point = Subtract(center, Scale(face, 0.5f * (gap + circle.radius)));
        manifold.points[0].separation = separation;
        manifold.points[0].id = 0x4000u;
        manifold.count = 1;
        return manifold;
    }
}
