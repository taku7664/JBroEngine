#include <JBro/Physics2D/Geometry.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

// 2D 물리 커널의 도형 기하 테스트(D-199, physics-plan §4 의 1 단계).
// 기존 엔진의 오목 폴리곤 결함 중 기하에서 나온 것(절댓값 넓이의 관성, 일직선 점에서 멈추는 귀 자르기,
// 조각이 없으면 오목 통짜를 쓰는 폴백)을 숫자로 붙잡는다.
namespace
{
    using JBro::Array;
    using JBro::ArrayView;
    using JBro::Vec2;
    using JBro::Physics2D::ConvexPolygon;
    using JBro::Physics2D::MassData;
    using JBro::Physics2D::PolygonError;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    bool Near(float actual, float expected, float tolerance)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    ArrayView<const Vec2> View(const Array<Vec2>& points)
    {
        return points.View();
    }

    // 3x3 정사각형에서 가운데 위쪽 1x2 를 판 U. 면적 중심 (1.5, 1.357) 이 파인 홈 안, 즉 도형 밖에 있다.
    Array<Vec2> MakeU()
    {
        return { { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
    }

    Array<Vec2> MakeL()
    {
        return { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
    }

    bool IsStrictlyConvexCcw(const ConvexPolygon& polygon)
    {
        if (polygon.count < 3 || polygon.count > JBro::Physics2D::MaxPolygonVertices)
        {
            return false;
        }
        for (std::uint32_t i = 0; i < polygon.count; ++i)
        {
            const Vec2 a = polygon.points[(i + polygon.count - 1) % polygon.count];
            const Vec2 b = polygon.points[i];
            const Vec2 c = polygon.points[(i + 1) % polygon.count];
            const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
            if (cross <= 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    float PieceAreaSum(const Array<ConvexPolygon>& pieces)
    {
        float sum = 0.0f;
        for (const ConvexPolygon& piece : pieces)
        {
            sum += JBro::Physics2D::SignedArea(ArrayView<const Vec2>(piece.points, piece.count));
        }
        return sum;
    }

    MassData PieceMass(const Array<ConvexPolygon>& pieces, float density)
    {
        Array<MassData> parts;
        for (const ConvexPolygon& piece : pieces)
        {
            parts.Add(JBro::Physics2D::ComputePolygonMass(piece, density));
        }
        return JBro::Physics2D::CombineMass(parts.View());
    }

    // 짝홀 규칙. 외곽선이 시계든 반시계든 같다.
    bool PointInOutline(Vec2 point, const Array<Vec2>& outline)
    {
        bool inside = false;
        const std::size_t count = outline.Size();
        for (std::size_t i = 0, j = count - 1; i < count; j = i++)
        {
            const Vec2 a = outline[i];
            const Vec2 b = outline[j];
            if ((a.y > point.y) != (b.y > point.y)
                && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            {
                inside = !inside;
            }
        }
        return inside;
    }

    bool PointInPiece(Vec2 point, const ConvexPolygon& piece)
    {
        for (std::uint32_t i = 0; i < piece.count; ++i)
        {
            const Vec2 a = piece.points[i];
            const Vec2 b = piece.points[(i + 1) % piece.count];
            if ((b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x) <= 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    // **조각이 도형을 정확히 한 겹으로 덮는다.** 넓이 합만으로는 부족하다 - 조각이 도형 밖으로 삐져나가고 그만큼
    // 다른 곳이 겹쳐도 부호 있는 넓이의 합은 같다. 격자점마다 "외곽선 안이면 조각 하나, 밖이면 조각 없음" 을 본다.
    // 격자를 무리수 비율로 어긋나게 두어 점이 변 위에 떨어지지 않게 한다. 두 축의 어긋남을 더해 정수가 되면
    // `x + y = 정수` 인 대각선 위에 점이 줄지어 떨어진다(첫 판이 0.382 + 0.618 = 1 로 U 의 대각선을 밟았다).
    void CheckCoverage(const Array<Vec2>& outline, const Array<ConvexPolygon>& pieces, const char* message)
    {
        Vec2 low = outline[0];
        Vec2 high = outline[0];
        for (const Vec2& point : outline)
        {
            low = { std::fmin(low.x, point.x), std::fmin(low.y, point.y) };
            high = { std::fmax(high.x, point.x), std::fmax(high.y, point.y) };
        }
        constexpr int Steps = 41;
        for (int ix = 0; ix < Steps; ++ix)
        {
            for (int iy = 0; iy < Steps; ++iy)
            {
                const Vec2 point = {
                    low.x + (high.x - low.x) * (static_cast<float>(ix) + 0.3819660f) / Steps,
                    low.y + (high.y - low.y) * (static_cast<float>(iy) + 0.2360680f) / Steps };
                int covering = 0;
                for (const ConvexPolygon& piece : pieces)
                {
                    if (PointInPiece(point, piece))
                    {
                        ++covering;
                    }
                }
                if (covering != (PointInOutline(point, outline) ? 1 : 0))
                {
                    std::cout << "point (" << point.x << ", " << point.y << ") is covered " << covering
                        << " times; pieces:\n";
                    for (const ConvexPolygon& piece : pieces)
                    {
                        for (std::uint32_t k = 0; k < piece.count; ++k)
                        {
                            std::cout << " (" << piece.points[k].x << ", " << piece.points[k].y << ")";
                        }
                        std::cout << '\n';
                    }
                }
                Check(covering == (PointInOutline(point, outline) ? 1 : 0), message);
            }
        }
    }

    // 분해가 지켜야 할 것을 한 자리에서 본다: 성공, 모든 조각이 엄격한 볼록·반시계·8 점 이하, 넓이 합 = 원래 넓이,
    // 도형을 한 겹으로 덮음.
    void CheckDecomposition(const Array<Vec2>& outline, const char* message)
    {
        Array<ConvexPolygon> pieces;
        Check(JBro::Physics2D::DecomposePolygon(View(outline), pieces) == PolygonError::None, message);
        Check(false == pieces.IsEmpty(), message);
        for (const ConvexPolygon& piece : pieces)
        {
            Check(IsStrictlyConvexCcw(piece), message);
        }
        const float expected = std::fabs(JBro::Physics2D::SignedArea(View(outline)));
        Check(Near(PieceAreaSum(pieces), expected, expected * 1.0e-4f + 1.0e-5f), message);
        CheckCoverage(outline, pieces, message);
    }

    // **부호 있는 넓이와 외곽선 질량이 오목 도형에서 맞다.** 기존 엔진은 부채꼴 삼각형의 절댓값을 더해 U 의 넓이를
    // 11 로 쟀고(참값 7), 정사각형의 관성도 두 배로 냈다(physics-plan §1.2 의 4).
    void TestOutlineMassOfTheUMatchesTheAnalyticValue()
    {
        const Array<Vec2> u = MakeU();
        Check(Near(JBro::Physics2D::SignedArea(View(u)), 7.0f, 1.0e-5f), "the U has area 7, not 11");

        const MassData mass = JBro::Physics2D::ComputeOutlineMass(View(u), 1.0f);
        Check(Near(mass.mass, 7.0f, 1.0e-5f), "unit density gives mass 7");
        Check(Near(mass.center.x, 1.5f, 1.0e-5f), "the centroid is on the symmetry axis");
        Check(Near(mass.center.y, 9.5f / 7.0f, 1.0e-5f), "and at y = 9.5 / 7, inside the notch");
        // 3x3 정사각형(중심 관성 13.5) 에서 1x2 홈(중심 관성 5/6) 을 빼고 평행축 정리로 합친 값.
        const float cy = 9.5f / 7.0f;
        const float expected = (13.5f + 9.0f * (1.5f - cy) * (1.5f - cy))
            - (5.0f / 6.0f + 2.0f * (2.0f - cy) * (2.0f - cy));
        Check(Near(mass.inertia, expected, 1.0e-4f), "the inertia about the centroid is the analytic value");

        ConvexPolygon square;
        square.points[0] = { -0.5f, -0.5f };
        square.points[1] = { 0.5f, -0.5f };
        square.points[2] = { 0.5f, 0.5f };
        square.points[3] = { -0.5f, 0.5f };
        square.count = 4;
        const MassData squareMass = JBro::Physics2D::ComputePolygonMass(square, 1.0f);
        Check(Near(squareMass.inertia, 1.0f / 6.0f, 1.0e-6f), "a unit square has inertia m/6, not m/3");
        Check(Near(squareMass.center.x, 0.0f, 1.0e-6f) && Near(squareMass.center.y, 0.0f, 1.0e-6f),
            "and its centroid at its middle");
    }

    // **조각을 모은 질량이 외곽선에서 바로 구한 질량과 같다.** 두 계산은 서로 다른 길이라, 분해가 넓이·중심·관성을
    // 잃었으면 여기서 어긋난다.
    void TestPieceMassEqualsOutlineMass()
    {
        const Array<Vec2> shapes[] = { MakeU(), MakeL() };
        for (const Array<Vec2>& outline : shapes)
        {
            Array<ConvexPolygon> pieces;
            Check(JBro::Physics2D::DecomposePolygon(View(outline), pieces) == PolygonError::None,
                "the concave outline decomposes");
            Check(pieces.Size() >= 2, "a concave outline needs more than one piece");
            CheckDecomposition(outline, "the concave outline is covered exactly once");
            // U 는 두 기둥과 바닥 사다리꼴 셋, L 은 둘이 최소다. 병합이 없으면 삼각형 여섯·넷이 남는다 -
            // 조각 사이 이음매가 늘수록 유령 충돌이 날 자리도 는다(physics-plan §3.2).
            Check(pieces.Size() == (outline.Size() == 8 ? 3u : 2u), "the merge reaches the fewest convex pieces");

            const MassData fromPieces = PieceMass(pieces, 2.0f);
            const MassData fromOutline = JBro::Physics2D::ComputeOutlineMass(View(outline), 2.0f);
            Check(Near(fromPieces.mass, fromOutline.mass, 1.0e-4f), "the pieces keep the mass");
            Check(Near(fromPieces.center.x, fromOutline.center.x, 1.0e-4f)
                && Near(fromPieces.center.y, fromOutline.center.y, 1.0e-4f), "and the centroid");
            Check(Near(fromPieces.inertia, fromOutline.inertia, 1.0e-3f), "and the inertia");
        }
    }

    // **볼록 입력은 나누지 않는다.** 볼록 바닥을 조각으로 나누면 접촉면 변이 없는 조각이 생긴다 -
    // 기존 엔진이 주석으로 남긴 수평 법선 버그다(`Physics2DSystem.cpp:3048`).
    void TestAConvexOutlineStaysWhole()
    {
        const Array<Vec2> box = { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 0, 1 } };
        Array<ConvexPolygon> pieces;
        Check(JBro::Physics2D::DecomposePolygon(View(box), pieces) == PolygonError::None, "a box decomposes");
        Check(pieces.Size() == 1 && pieces[0].count == 4, "into itself");

        // 8 점을 넘는 볼록 다각형은 상한 때문에 나뉘지만, 조각마다 여전히 볼록이고 넓이가 남는다.
        Array<Vec2> circle;
        for (int i = 0; i < 20; ++i)
        {
            const float angle = 6.2831853f * static_cast<float>(i) / 20.0f;
            circle.Add({ std::cos(angle), std::sin(angle) });
        }
        CheckDecomposition(circle, "a 20-gon splits into convex pieces of at most eight points");
        Array<ConvexPolygon> circlePieces;
        JBro::Physics2D::DecomposePolygon(View(circle), circlePieces);
        Check(circlePieces.Size() == 3, "twenty points fit in three pieces of eight, and not fewer");
    }

    // **일직선 점과 중복 점은 넓이에 구멍을 내지 않는다.** 기존 엔진의 귀 자르기는 일직선 꼭짓점을 건너뛰고,
    // 귀를 못 찾으면 남은 부분을 버리고 나왔다(physics-plan §1.2).
    void TestCollinearAndDuplicatePointsLeaveNoHole()
    {
        // U 의 모든 변에 가운데 점을 넣고, 몇 점은 두 번 적는다.
        const Array<Vec2> noisy = {
            { 0, 0 }, { 1.5f, 0 }, { 3, 0 }, { 3, 0 }, { 3, 1.5f }, { 3, 3 }, { 2.5f, 3 }, { 2, 3 },
            { 2, 2 }, { 2, 1 }, { 1.5f, 1 }, { 1, 1 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 0.5f, 3 }, { 0, 3 },
            { 0, 1.5f }, { 0, 0 } };
        CheckDecomposition(noisy, "a noisy U decomposes without losing area");

        Array<Vec2> clean;
        Check(JBro::Physics2D::CleanPolygon(View(noisy), clean) == PolygonError::None, "the noisy U cleans");
        Check(clean.Size() == 8, "back to the eight corners of the U");
        Check(Near(JBro::Physics2D::SignedArea(clean.View()), 7.0f, 1.0e-5f), "with the same area");
    }

    // **감긴 방향이 시계여도 결과는 같다.** 편집기에서 꼭짓점을 어느 방향으로 찍든 도형은 같아야 한다.
    void TestClockwiseInputIsNormalized()
    {
        Array<Vec2> reversed = MakeU();
        std::reverse(reversed.begin(), reversed.end());
        Check(JBro::Physics2D::SignedArea(View(reversed)) < 0.0f, "the reversed U is clockwise");
        CheckDecomposition(reversed, "a clockwise U decomposes to counter-clockwise pieces");

        Array<ConvexPolygon> pieces;
        JBro::Physics2D::DecomposePolygon(View(reversed), pieces);
        Check(Near(PieceAreaSum(pieces), 7.0f, 1.0e-4f), "and keeps area 7");
    }

    // **단순 다각형이 아니면 도형을 만들지 않는다.** 조각 없이 오목 통짜로 떨어지는 폴백(기존 엔진)은 두지 않는다.
    void TestInvalidOutlinesAreRefused()
    {
        Array<ConvexPolygon> pieces;
        pieces.Emplace();

        const Array<Vec2> bowTie = { { 0, 0 }, { 2, 2 }, { 2, 0 }, { 0, 2 } };
        Check(JBro::Physics2D::DecomposePolygon(View(bowTie), pieces) == PolygonError::SelfIntersecting,
            "a bow tie crosses itself");
        Check(pieces.IsEmpty(), "and a refused outline leaves no pieces");

        // 두 정사각형이 한 꼭짓점에서만 만난다. 넓이는 있지만 단순 다각형이 아니다.
        const Array<Vec2> pinched = {
            { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } };
        Check(JBro::Physics2D::DecomposePolygon(View(pinched), pieces) == PolygonError::SelfIntersecting,
            "two squares touching at a corner are not a simple polygon");

        const Array<Vec2> twoPoints = { { 0, 0 }, { 1, 0 } };
        Check(JBro::Physics2D::DecomposePolygon(View(twoPoints), pieces) == PolygonError::TooFewPoints,
            "two points are too few");

        const Array<Vec2> line = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } };
        Check(JBro::Physics2D::DecomposePolygon(View(line), pieces) == PolygonError::TooFewPoints,
            "points on one line clean down to fewer than three");

        const Array<Vec2> samePoint = { { 1, 1 }, { 1, 1 }, { 1, 1 } };
        Check(JBro::Physics2D::DecomposePolygon(View(samePoint), pieces) == PolygonError::TooFewPoints,
            "one point written three times is one point");
    }

    // **무작위 별 모양 200 개.** 각도를 정렬한 반지름 무작위 도형은 늘 단순 다각형이고 대개 오목하다.
    // 손으로 고른 도형이 놓치는 배치를 훑는다. 씨앗이 고정이라 실패는 다시 난다.
    void TestRandomStarPolygonsDecomposeWithoutLoss()
    {
        std::uint32_t state = 0x12345678u;
        const auto next = [&state]()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
        };

        int concaveCount = 0;
        for (int shape = 0; shape < 200; ++shape)
        {
            const int count = 5 + static_cast<int>(next() * 20.0f);
            Array<Vec2> outline;
            for (int i = 0; i < count; ++i)
            {
                const float angle = 6.2831853f * (static_cast<float>(i) + 0.8f * next()) / static_cast<float>(count);
                const float radius = 0.3f + 2.0f * next();
                outline.Add({ radius * std::cos(angle), radius * std::sin(angle) });
            }

            Array<ConvexPolygon> pieces;
            Check(JBro::Physics2D::DecomposePolygon(View(outline), pieces) == PolygonError::None,
                "every random star polygon decomposes");
            if (pieces.Size() > 1)
            {
                ++concaveCount;
            }
            for (const ConvexPolygon& piece : pieces)
            {
                Check(IsStrictlyConvexCcw(piece), "every random piece is strictly convex and small enough");
            }

            Array<Vec2> clean;
            JBro::Physics2D::CleanPolygon(View(outline), clean);
            const float expected = JBro::Physics2D::SignedArea(clean.View());
            if (false == Near(PieceAreaSum(pieces), expected, expected * 1.0e-4f))
            {
                std::cout << "random shape " << shape << ": " << clean.Size() << " points, " << pieces.Size()
                    << " pieces, area " << PieceAreaSum(pieces) << " instead of " << expected << '\n';
                for (const Vec2& point : clean)
                {
                    std::cout << "  (" << point.x << ", " << point.y << ")\n";
                }
            }
            Check(Near(PieceAreaSum(pieces), expected, expected * 1.0e-4f), "no random piece set loses area");
            CheckCoverage(clean, pieces, "every random piece set covers its outline exactly once");

            const MassData fromPieces = PieceMass(pieces, 1.0f);
            const MassData fromOutline = JBro::Physics2D::ComputeOutlineMass(clean.View(), 1.0f);
            Check(Near(fromPieces.inertia, fromOutline.inertia, fromOutline.inertia * 1.0e-3f),
                "and no random piece set loses inertia");
        }
        Check(concaveCount > 100, "most random stars need several pieces, so the test really exercises the ear clipper");
    }

    void TestCircleAndCombinedMass()
    {
        const MassData circle = JBro::Physics2D::ComputeCircleMass({ 1, 0 }, 0.5f, 4.0f);
        Check(Near(circle.mass, 3.14159265f, 1.0e-5f), "a circle of radius 0.5 at density 4 has mass pi");
        Check(Near(circle.inertia, 0.5f * circle.mass * 0.25f, 1.0e-6f), "and inertia m r^2 / 2");

        // 같은 원 두 개를 x = ±1 에 두면 중심은 원점이고 관성은 평행축 정리로 m·1² 씩 는다.
        const MassData parts[] = { circle, JBro::Physics2D::ComputeCircleMass({ -1, 0 }, 0.5f, 4.0f) };
        const MassData combined = JBro::Physics2D::CombineMass(ArrayView<const MassData>(parts));
        Check(Near(combined.mass, 2.0f * circle.mass, 1.0e-5f), "masses add");
        Check(Near(combined.center.x, 0.0f, 1.0e-6f), "the combined center is between them");
        Check(Near(combined.inertia, 2.0f * (circle.inertia + circle.mass), 1.0e-5f),
            "and each inertia moves by m d^2");
        Check(JBro::Physics2D::CombineMass(ArrayView<const MassData>()).mass == 0.0f, "nothing weighs nothing");
    }
}

int RunPhysics2DGeometryTests()
{
    TestOutlineMassOfTheUMatchesTheAnalyticValue();
    TestPieceMassEqualsOutlineMass();
    TestAConvexOutlineStaysWhole();
    TestCollinearAndDuplicatePointsLeaveNoHole();
    TestClockwiseInputIsNormalized();
    TestInvalidOutlinesAreRefused();
    TestRandomStarPolygonsDecomposeWithoutLoss();
    TestCircleAndCombinedMass();
    std::cout << "Physics2D geometry tests passed.\n";
    return 0;
}
