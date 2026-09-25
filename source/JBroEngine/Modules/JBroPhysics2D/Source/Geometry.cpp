#include <JBro/Physics2D/Geometry.h>

#include <algorithm>
#include <cmath>

namespace JBro::Physics2D
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

        Vec2 Subtract(Vec2 a, Vec2 b)
        {
            return { a.x - b.x, a.y - b.y };
        }

        float Cross(Vec2 a, Vec2 b)
        {
            return a.x * b.y - a.y * b.x;
        }

        float Dot(Vec2 a, Vec2 b)
        {
            return a.x * b.x + a.y * b.y;
        }

        float LengthSquared(Vec2 a)
        {
            return a.x * a.x + a.y * a.y;
        }

        // b 가 a 와 c 를 잇는 직선에서 LinearSlop 안쪽이면 일직선이다. a 와 c 가 같은 점이면
        // b 는 되돌아가는 가시이고 넓이가 없으므로 역시 뺄 점이다.
        bool IsCollinear(Vec2 a, Vec2 b, Vec2 c)
        {
            const Vec2 ac = Subtract(c, a);
            const float acLengthSquared = LengthSquared(ac);
            if (acLengthSquared <= LinearSlop * LinearSlop)
            {
                return true;
            }
            const float cross = Cross(ac, Subtract(b, a));
            return cross * cross <= LinearSlop * LinearSlop * acLengthSquared;
        }

        // 반시계에서 b 가 왼쪽으로 꺾이는지 부호로만 본다. 허용 오차로 거르지 않는다 - 거의 일직선인 꼭짓점을
        // 볼록이 아니라고 보면 영영 귀가 되지 못하고, 그렇다고 빼 버리면 그만큼의 얇은 넓이가 사라진다.
        bool IsConvexTurn(Vec2 a, Vec2 b, Vec2 c)
        {
            return Cross(Subtract(b, a), Subtract(c, b)) > 0.0f;
        }

        // 넓이가 사실상 없는 꼭짓점이다. 외곽선 정리(LinearSlop)와 달리, 분해 도중 여기서 빼는 것은 넓이를 잃지 않는다.
        bool IsDegenerate(Vec2 a, Vec2 b, Vec2 c)
        {
            const float cross = Cross(Subtract(b, a), Subtract(c, b));
            return std::fabs(cross) <= 1.0e-6f * LengthSquared(Subtract(c, a));
        }

        bool OnSegment(Vec2 a, Vec2 b, Vec2 p)
        {
            return p.x >= std::fmin(a.x, b.x) && p.x <= std::fmax(a.x, b.x)
                && p.y >= std::fmin(a.y, b.y) && p.y <= std::fmax(a.y, b.y);
        }

        // 닿기만 해도 교차로 본다. 이웃하지 않는 두 변이 닿으면 단순 다각형이 아니다.
        bool SegmentsTouch(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
        {
            const float o1 = Cross(Subtract(b, a), Subtract(c, a));
            const float o2 = Cross(Subtract(b, a), Subtract(d, a));
            const float o3 = Cross(Subtract(d, c), Subtract(a, c));
            const float o4 = Cross(Subtract(d, c), Subtract(b, c));
            if (((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f))
                && ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f)))
            {
                return true;
            }
            if (o1 == 0.0f && OnSegment(a, b, c))
            {
                return true;
            }
            if (o2 == 0.0f && OnSegment(a, b, d))
            {
                return true;
            }
            if (o3 == 0.0f && OnSegment(c, d, a))
            {
                return true;
            }
            return o4 == 0.0f && OnSegment(c, d, b);
        }

        // 경계를 포함한다. 꼭짓점이 대각선 위에 놓이면 그 대각선은 쓸 수 없다.
        bool PointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c)
        {
            const float d1 = Cross(Subtract(b, a), Subtract(p, a));
            const float d2 = Cross(Subtract(c, b), Subtract(p, b));
            const float d3 = Cross(Subtract(a, c), Subtract(p, c));
            return d1 >= 0.0f && d2 >= 0.0f && d3 >= 0.0f;
        }

        bool IsSimple(const Array<Vec2>& points)
        {
            const std::size_t count = points.Size();
            for (std::size_t i = 0; i < count; ++i)
            {
                const Vec2 a = points[i];
                const Vec2 b = points[(i + 1) % count];
                // i 의 이웃 변(i-1, i+1)은 끝점을 공유하므로 건너뛴다.
                for (std::size_t j = i + 2; j < count; ++j)
                {
                    if (i == 0 && j == count - 1)
                    {
                        continue;
                    }
                    if (SegmentsTouch(a, b, points[j], points[(j + 1) % count]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        // 조각은 정리된 외곽선 꼭짓점의 번호 고리다. 번호로 들고 있어야 두 조각이 공유하는 대각선을 찾을 수 있다.
        struct Piece
        {
            std::uint32_t vertices[MaxPolygonVertices];
            std::uint32_t count = 0;
            bool          alive = true;
        };

        bool IsConvexRing(const Array<Vec2>& points, const std::uint32_t* ring, std::uint32_t count)
        {
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const Vec2 a = points[ring[(i + count - 1) % count]];
                const Vec2 b = points[ring[i]];
                const Vec2 c = points[ring[(i + 1) % count]];
                if (false == IsConvexTurn(a, b, c))
                {
                    return false;
                }
            }
            return true;
        }

        // a 의 변 (i → j) 와 b 의 변 (j → i) 가 같은 대각선이면 둘을 이어 하나의 고리로 만든다.
        // 상한을 넘거나 볼록이 아니면 false 이고 merged 는 쓰지 않는다.
        bool TryMerge(const Array<Vec2>& points, const Piece& a, const Piece& b, Piece& merged)
        {
            for (std::uint32_t ea = 0; ea < a.count; ++ea)
            {
                const std::uint32_t i = a.vertices[ea];
                const std::uint32_t j = a.vertices[(ea + 1) % a.count];
                for (std::uint32_t eb = 0; eb < b.count; ++eb)
                {
                    if (b.vertices[eb] != j || b.vertices[(eb + 1) % b.count] != i)
                    {
                        continue;
                    }

                    const std::uint32_t total = a.count + b.count - 2;
                    if (total > MaxPolygonVertices)
                    {
                        return false;
                    }

                    // j 에서 출발해 a 를 한 바퀴 돌면 i 에서 끝난다. 이어서 b 를 i 다음부터 j 앞까지 돈다.
                    Piece candidate;
                    for (std::uint32_t k = 0; k < a.count; ++k)
                    {
                        candidate.vertices[candidate.count] = a.vertices[(ea + 1 + k) % a.count];
                        ++candidate.count;
                    }
                    for (std::uint32_t k = 2; k < b.count; ++k)
                    {
                        candidate.vertices[candidate.count] = b.vertices[(eb + k) % b.count];
                        ++candidate.count;
                    }

                    if (false == IsConvexRing(points, candidate.vertices, candidate.count))
                    {
                        return false;
                    }
                    merged = candidate;
                    return true;
                }
            }
            return false;
        }

        bool Triangulate(const Array<Vec2>& points, Array<Piece>& pieces)
        {
            Array<std::uint32_t> ring;
            ring.Reserve(points.Size());
            for (std::uint32_t i = 0; i < points.Size(); ++i)
            {
                ring.Add(i);
            }

            while (ring.Size() > 3)
            {
                bool clipped = false;
                const std::size_t remaining = ring.Size();
                for (std::size_t i = 0; i < remaining; ++i)
                {
                    const std::uint32_t previous = ring[(i + remaining - 1) % remaining];
                    const std::uint32_t current = ring[i];
                    const std::uint32_t next = ring[(i + 1) % remaining];
                    const Vec2 a = points[previous];
                    const Vec2 b = points[current];
                    const Vec2 c = points[next];

                    // 귀를 잘라 내면 남은 고리에 일직선 꼭짓점이 생길 수 있다. 넓이가 없으므로 삼각형을 내지 않고
                    // 빼기만 한다. 기존 엔진은 이 꼭짓점을 영원히 건너뛰어 남은 넓이를 버린 채 멈췄다(physics-plan §1.2).
                    if (IsDegenerate(a, b, c))
                    {
                        ring.RemoveAt(i);
                        clipped = true;
                        break;
                    }
                    if (false == IsConvexTurn(a, b, c))
                    {
                        continue;
                    }

                    bool isEar = true;
                    for (std::size_t k = 0; k < remaining; ++k)
                    {
                        const std::uint32_t other = ring[k];
                        if (other == previous || other == current || other == next)
                        {
                            continue;
                        }
                        if (PointInTriangle(points[other], a, b, c))
                        {
                            isEar = false;
                            break;
                        }
                    }
                    if (false == isEar)
                    {
                        continue;
                    }

                    Piece& triangle = pieces.Emplace();
                    triangle.vertices[0] = previous;
                    triangle.vertices[1] = current;
                    triangle.vertices[2] = next;
                    triangle.count = 3;
                    ring.RemoveAt(i);
                    clipped = true;
                    break;
                }

                if (false == clipped)
                {
                    return false;
                }
            }

            const Vec2 a = points[ring[0]];
            const Vec2 b = points[ring[1]];
            const Vec2 c = points[ring[2]];
            if (false == IsDegenerate(a, b, c))
            {
                Piece& triangle = pieces.Emplace();
                triangle.vertices[0] = ring[0];
                triangle.vertices[1] = ring[1];
                triangle.vertices[2] = ring[2];
                triangle.count = 3;
            }
            return true;
        }
    }

    float SignedArea(ArrayView<const Vec2> points)
    {
        const std::size_t count = points.Size();
        float twiceArea = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            twiceArea += Cross(points[i], points[(i + 1) % count]);
        }
        return twiceArea * 0.5f;
    }

    PolygonError CleanPolygon(ArrayView<const Vec2> points, Array<Vec2>& out)
    {
        out.Clear();
        for (const Vec2& point : points)
        {
            if (false == out.IsEmpty()
                && LengthSquared(Subtract(point, out.Last())) <= LinearSlop * LinearSlop)
            {
                continue;
            }
            out.Add(point);
        }
        while (out.Size() > 1
            && LengthSquared(Subtract(out.Last(), out.First())) <= LinearSlop * LinearSlop)
        {
            out.RemoveAt(out.Size() - 1);
        }

        // 하나를 빼면 이웃이 새로 일직선이 될 수 있으므로 더 뺄 것이 없을 때까지 돈다.
        bool removed = true;
        while (removed && out.Size() >= 3)
        {
            removed = false;
            const std::size_t count = out.Size();
            for (std::size_t i = 0; i < count; ++i)
            {
                const Vec2 a = out[(i + count - 1) % count];
                const Vec2 b = out[i];
                const Vec2 c = out[(i + 1) % count];
                if (IsCollinear(a, b, c))
                {
                    out.RemoveAt(i);
                    removed = true;
                    break;
                }
            }
        }

        if (out.Size() < 3)
        {
            out.Clear();
            return PolygonError::TooFewPoints;
        }

        // 넓이보다 먼저 본다. 8 자 모양은 두 고리의 넓이가 서로 지워 0 이 될 수 있는데, 그것은 넓이가 없는 도형이
        // 아니라 교차하는 도형이다.
        if (false == IsSimple(out))
        {
            out.Clear();
            return PolygonError::SelfIntersecting;
        }

        const float area = SignedArea(out.View());
        if (std::fabs(area) <= LinearSlop * LinearSlop)
        {
            out.Clear();
            return PolygonError::ZeroArea;
        }
        if (area < 0.0f)
        {
            std::reverse(out.begin(), out.end());
        }

        return PolygonError::None;
    }

    PolygonError DecomposePolygon(ArrayView<const Vec2> points, Array<ConvexPolygon>& outPieces)
    {
        outPieces.Clear();

        Array<Vec2> clean;
        const PolygonError error = CleanPolygon(points, clean);
        if (error != PolygonError::None)
        {
            return error;
        }

        Array<Piece> pieces;
        pieces.Reserve(clean.Size());
        if (clean.Size() <= MaxPolygonVertices)
        {
            Piece whole;
            for (std::uint32_t i = 0; i < clean.Size(); ++i)
            {
                whole.vertices[i] = i;
            }
            whole.count = static_cast<std::uint32_t>(clean.Size());
            if (IsConvexRing(clean, whole.vertices, whole.count))
            {
                pieces.Add(whole);
            }
        }

        if (pieces.IsEmpty())
        {
            if (false == Triangulate(clean, pieces))
            {
                return PolygonError::DecompositionFailed;
            }

            // Hertel-Mehlhorn: 대각선 하나를 지워도 볼록이고 상한 안이면 두 조각을 합친다. 합칠 것이 없을 때까지.
            bool merged = true;
            while (merged)
            {
                merged = false;
                for (std::size_t a = 0; a < pieces.Size() && false == merged; ++a)
                {
                    if (false == pieces[a].alive)
                    {
                        continue;
                    }
                    for (std::size_t b = a + 1; b < pieces.Size(); ++b)
                    {
                        if (false == pieces[b].alive)
                        {
                            continue;
                        }
                        Piece combined;
                        if (TryMerge(clean, pieces[a], pieces[b], combined))
                        {
                            pieces[a] = combined;
                            pieces[b].alive = false;
                            merged = true;
                            break;
                        }
                    }
                }
            }
        }

        for (const Piece& piece : pieces)
        {
            if (false == piece.alive)
            {
                continue;
            }
            ConvexPolygon& polygon = outPieces.Emplace();
            for (std::uint32_t k = 0; k < piece.count; ++k)
            {
                polygon.points[k] = clean[piece.vertices[k]];
            }
            polygon.count = piece.count;
        }
        return PolygonError::None;
    }

    MassData ComputePolygonMass(const ConvexPolygon& polygon, float density)
    {
        MassData result;
        if (polygon.count < 3)
        {
            return result;
        }

        // 첫 꼭짓점을 기준으로 부채꼴 삼각형을 더한다(Box2D 와 같은 방식). 볼록이라 모든 삼각형의 부호가 같다.
        const Vec2 origin = polygon.points[0];
        float area = 0.0f;
        Vec2 center;
        float inertiaAboutOrigin = 0.0f;
        for (std::uint32_t i = 1; i + 1 < polygon.count; ++i)
        {
            const Vec2 e1 = Subtract(polygon.points[i], origin);
            const Vec2 e2 = Subtract(polygon.points[i + 1], origin);
            const float d = Cross(e1, e2);
            const float triangleArea = 0.5f * d;
            area += triangleArea;
            center.x += triangleArea * (e1.x + e2.x) / 3.0f;
            center.y += triangleArea * (e1.y + e2.y) / 3.0f;
            const float intx2 = e1.x * e1.x + e2.x * e1.x + e2.x * e2.x;
            const float inty2 = e1.y * e1.y + e2.y * e1.y + e2.y * e2.y;
            inertiaAboutOrigin += (0.25f / 3.0f * d) * (intx2 + inty2);
        }
        if (area <= 0.0f)
        {
            return result;
        }

        center.x /= area;
        center.y /= area;
        result.mass = density * area;
        result.center = { origin.x + center.x, origin.y + center.y };
        // 평행축 정리: 기준점 관성 = 중심 관성 + m·|중심 - 기준점|².
        result.inertia = density * inertiaAboutOrigin - result.mass * LengthSquared(center);
        return result;
    }

    MassData ComputeOutlineMass(ArrayView<const Vec2> ccwPoints, float density)
    {
        MassData result;
        const std::size_t count = ccwPoints.Size();
        if (count < 3)
        {
            return result;
        }

        float twiceArea = 0.0f;
        Vec2 weighted;
        float inertiaSum = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vec2 p = ccwPoints[i];
            const Vec2 q = ccwPoints[(i + 1) % count];
            // 부호를 버리지 않는다. 오목 도형에서는 원점에서 편 삼각형 일부가 음수여야 넓이가 맞는다.
            const float cross = Cross(p, q);
            twiceArea += cross;
            weighted.x += (p.x + q.x) * cross;
            weighted.y += (p.y + q.y) * cross;
            inertiaSum += cross * (Dot(p, p) + Dot(p, q) + Dot(q, q));
        }
        const float area = twiceArea * 0.5f;
        if (area <= 0.0f)
        {
            return result;
        }

        result.mass = density * area;
        result.center = { weighted.x / (6.0f * area), weighted.y / (6.0f * area) };
        const float inertiaAboutOrigin = density * inertiaSum / 12.0f;
        result.inertia = inertiaAboutOrigin - result.mass * LengthSquared(result.center);
        return result;
    }

    MassData ComputeCircleMass(Vec2 center, float radius, float density)
    {
        MassData result;
        if (radius <= 0.0f)
        {
            return result;
        }
        result.mass = density * Pi * radius * radius;
        result.center = center;
        result.inertia = 0.5f * result.mass * radius * radius;
        return result;
    }

    MassData CombineMass(ArrayView<const MassData> parts)
    {
        MassData result;
        for (const MassData& part : parts)
        {
            result.mass += part.mass;
            result.center.x += part.mass * part.center.x;
            result.center.y += part.mass * part.center.y;
        }
        if (result.mass <= 0.0f)
        {
            return MassData{};
        }
        result.center.x /= result.mass;
        result.center.y /= result.mass;
        for (const MassData& part : parts)
        {
            result.inertia += part.inertia
                + part.mass * LengthSquared(Subtract(part.center, result.center));
        }
        return result;
    }
}
