#include <JBro/Physics2D/BroadPhase.h>
#include <JBro/Physics2D/Collision.h>
#include <JBro/Physics2D/Geometry.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

// 2D 물리 커널의 좁은 판정과 브로드페이즈 테스트(D-199, physics-plan §4 의 2 단계).
// 기존 엔진의 오목 폴리곤 결함 중 판정에서 나온 것(도형 중심으로 법선 뒤집기, 통짜 오목 도형 클리핑,
// 한 쌍의 조각 결과를 하나로 줄이기)을 U·L 자 도형으로 붙잡는다.
namespace
{
    using JBro::Array;
    using JBro::ArrayView;
    using JBro::Rect;
    using JBro::Vec2;
    using JBro::Physics2D::Circle;
    using JBro::Physics2D::ConvexPolygon;
    using JBro::Physics2D::Manifold;
    using JBro::Physics2D::Pose;
    using JBro::Physics2D::ProxyPair;

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

    bool NearVector(Vec2 actual, Vec2 expected, float tolerance)
    {
        return Near(actual.x, expected.x, tolerance) && Near(actual.y, expected.y, tolerance);
    }

    Pose At(float x, float y, float angle = 0.0f)
    {
        Pose pose;
        pose.position = { x, y };
        pose.rotation = JBro::Physics2D::Rotation::FromAngle(angle);
        return pose;
    }

    ConvexPolygon MakeBox(float halfWidth, float halfHeight)
    {
        ConvexPolygon box;
        box.points[0] = { -halfWidth, -halfHeight };
        box.points[1] = { halfWidth, -halfHeight };
        box.points[2] = { halfWidth, halfHeight };
        box.points[3] = { -halfWidth, halfHeight };
        box.count = 4;
        return box;
    }

    Array<ConvexPolygon> Decompose(const Array<Vec2>& outline)
    {
        Array<ConvexPolygon> pieces;
        Check(JBro::Physics2D::DecomposePolygon(outline.View(), pieces) == JBro::Physics2D::PolygonError::None,
            "the test outline decomposes");
        return pieces;
    }

    // 정적 오목 도형(조각들) 대 상자 하나의 모든 매니폴드. 솔버가 받게 될 것과 같다 - 조각마다 따로다.
    Array<Manifold> CollidePiecesWithBox(
        const Array<ConvexPolygon>& pieces, const ConvexPolygon& box, const Pose& boxPose)
    {
        Array<Manifold> manifolds;
        for (const ConvexPolygon& piece : pieces)
        {
            const Manifold manifold = JBro::Physics2D::CollidePolygons(piece, At(0, 0), box, boxPose);
            if (manifold.count > 0)
            {
                manifolds.Add(manifold);
            }
        }
        return manifolds;
    }

    // **U 의 홈 안에서 안벽에 박힌 상자는 벽에서 밀려난다.** 기존 엔진은 법선을 U 의 면적 중심 (1.5, 1.357) 에서
    // 상자 중심으로 향하게 뒤집었고, 그 중심은 홈 안에 있어 왼쪽 안벽에서 법선이 -x 로 나왔다(physics-plan §1.2 의 1).
    void TestABoxInTheNotchOfAUIsPushedOutOfTheWall()
    {
        const Array<Vec2> u = {
            { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        const Array<ConvexPolygon> pieces = Decompose(u);
        const ConvexPolygon box = MakeBox(0.3f, 0.3f);

        const Array<Manifold> left = CollidePiecesWithBox(pieces, box, At(1.28f, 2.0f));
        Check(left.Size() == 1, "the box against the left inner wall touches one piece");
        Check(NearVector(left[0].normal, { 1, 0 }, 1.0e-5f), "and is pushed right, out of the wall");
        Check(left[0].count == 2, "a face against a face gives two points");
        for (std::uint32_t i = 0; i < left[0].count; ++i)
        {
            Check(Near(left[0].points[i].separation, -0.02f, 1.0e-4f), "each 0.02 deep");
            Check(Near(left[0].points[i].point.x, 0.99f, 1.0e-4f), "halfway between the wall and the box face");
            Check(left[0].points[i].point.y >= 1.7f - 1.0e-4f && left[0].points[i].point.y <= 2.3f + 1.0e-4f,
                "and within the box's side");
        }

        const Array<Manifold> right = CollidePiecesWithBox(pieces, box, At(1.72f, 2.0f));
        Check(right.Size() == 1 && NearVector(right[0].normal, { -1, 0 }, 1.0e-5f),
            "against the right inner wall the box is pushed left");

        const Array<Manifold> floor = CollidePiecesWithBox(pieces, box, At(1.5f, 1.28f));
        Check(floor.Size() == 1 && NearVector(floor[0].normal, { 0, 1 }, 1.0e-5f),
            "on the notch floor the box is pushed up");
    }

    // **순서를 바꾸면 법선만 뒤집힌다.** 법선은 언제나 A→B 이고 기준면에서만 나온다.
    void TestSwappingTheShapesFlipsOnlyTheNormal()
    {
        const Array<Vec2> u = {
            { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        const Array<ConvexPolygon> pieces = Decompose(u);
        const ConvexPolygon box = MakeBox(0.3f, 0.3f);
        int touched = 0;
        for (const ConvexPolygon& piece : pieces)
        {
            const Manifold forward = JBro::Physics2D::CollidePolygons(piece, At(0, 0), box, At(1.28f, 2.0f));
            const Manifold backward = JBro::Physics2D::CollidePolygons(box, At(1.28f, 2.0f), piece, At(0, 0));
            Check(forward.count == backward.count, "both orders find the same number of points");
            if (forward.count == 0)
            {
                continue;
            }
            ++touched;
            Check(NearVector(backward.normal, { -forward.normal.x, -forward.normal.y }, 1.0e-5f),
                "the reversed pair has the reversed normal");
            Check(Near(backward.points[0].separation, forward.points[0].separation, 1.0e-5f),
                "and the same depth");
        }
        Check(touched == 1, "exactly one piece is touched");

        // 상자(A)가 바닥(B) 위에 있으면 법선은 상자에서 바닥으로, 즉 아래로 향한다. 어느 쪽 면이 기준이든 A→B 다.
        const Manifold resting = JBro::Physics2D::CollidePolygons(
            MakeBox(0.5f, 0.5f), At(0, 0.49f), MakeBox(10.0f, 0.5f), At(0, -0.5f));
        Check(resting.count == 2, "a small box on a wide floor rests on two points");
        Check(NearVector(resting.normal, { 0, -1 }, 1.0e-5f), "and the normal still points from A to B");
        for (std::uint32_t i = 0; i < resting.count; ++i)
        {
            // 바닥의 윗면(길이 20)이 상자의 밑면(길이 1)으로 잘려야 한다. 자르지 않으면 점이 바닥 끝 x = ±10 에 선다.
            Check(resting.points[i].point.x >= -0.5f - 1.0e-4f && resting.points[i].point.x <= 0.5f + 1.0e-4f,
                "the wide floor face is clipped to the box's bottom");
        }

        // 돌린 상자(A)의 모서리가 바닥(B)에 박히면 덜 박힌 쪽은 바닥의 윗면이라 B 가 기준면을 낸다.
        // 그래도 법선은 A→B, 즉 아래다. 위의 경우들은 두 쪽의 깊이가 같아 늘 A 가 기준이었다.
        const float halfDiagonal = std::sqrt(0.5f);
        const Manifold flipped = JBro::Physics2D::CollidePolygons(
            MakeBox(0.5f, 0.5f), At(0, halfDiagonal - 0.01f, 0.78539816f), MakeBox(5.0f, 0.5f), At(0, -0.5f));
        Check(flipped.count == 1, "the corner touches at one point");
        Check(NearVector(flipped.normal, { 0, -1 }, 1.0e-4f), "with B's face as reference the normal is still A to B");
        Check(Near(flipped.points[0].separation, -0.01f, 1.0e-4f), "0.01 deep");
    }

    // **L 의 안쪽 모서리에 낀 원은 두 벽 모두에서 매니폴드를 받는다.** 기존 엔진은 조각별 결과 중 가장 얕은 것 하나만
    // 남기고 다시 쌍마다 법선 하나로 합쳐, 한쪽 벽만 밀었다(physics-plan §1.2 의 6).
    void TestACircleInTheInnerCornerOfAnLGetsBothWalls()
    {
        const Array<Vec2> l = { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        const Array<ConvexPolygon> pieces = Decompose(l);
        Check(pieces.Size() == 2, "the L is two pieces");

        Circle circle;
        circle.radius = 0.3f;
        bool sawFloor = false;
        bool sawWall = false;
        int count = 0;
        for (const ConvexPolygon& piece : pieces)
        {
            const Manifold manifold =
                JBro::Physics2D::CollidePolygonAndCircle(piece, At(0, 0), circle, At(1.25f, 1.25f));
            if (manifold.count == 0)
            {
                continue;
            }
            ++count;
            Check(Near(manifold.points[0].separation, -0.05f, 1.0e-4f), "each wall holds the circle 0.05 deep");
            if (NearVector(manifold.normal, { 0, 1 }, 1.0e-4f))
            {
                sawFloor = true;
            }
            if (NearVector(manifold.normal, { 1, 0 }, 1.0e-4f))
            {
                sawWall = true;
            }
        }
        Check(count == 2, "both pieces touch the circle");
        Check(sawFloor && sawWall, "one pushes it up and the other pushes it right");
    }

    // **원이 모서리 쪽에 있으면 법선은 모서리에서 원 중심으로 향한다**(보로노이 꼭짓점 영역). 변의 법선을 쓰면
    // 모서리를 지나가는 공이 옆으로 튕긴다. 중심이 도형 안에 들어오면 가장 얕은 변의 바깥으로 민다.
    void TestCircleAgainstACornerAndFromInside()
    {
        const ConvexPolygon box = MakeBox(0.5f, 0.5f);
        Circle ball;
        ball.radius = 0.2f;
        const Manifold corner = JBro::Physics2D::CollidePolygonAndCircle(box, At(0, 0), ball, At(0.6f, 0.6f));
        Check(corner.count == 1, "a ball at the corner touches");
        Check(NearVector(corner.normal, { 0.70710678f, 0.70710678f }, 1.0e-5f), "along the diagonal from the corner");
        Check(Near(corner.points[0].separation, std::sqrt(0.02f) - 0.2f, 1.0e-5f), "as deep as radius minus distance");

        const Manifold other = JBro::Physics2D::CollidePolygonAndCircle(box, At(0, 0), ball, At(-0.6f, 0.6f));
        Check(NearVector(other.normal, { -0.70710678f, 0.70710678f }, 1.0e-5f), "and the other corner points the other way");

        const Manifold inside = JBro::Physics2D::CollidePolygonAndCircle(box, At(0, 0), ball, At(0.3f, 0.1f));
        Check(inside.count == 1, "a ball whose center is inside still touches");
        Check(NearVector(inside.normal, { 1, 0 }, 1.0e-5f), "and leaves through the nearest face");
        Check(Near(inside.points[0].separation, -0.2f - 0.2f, 1.0e-5f), "center 0.2 inside plus the radius");
    }

    void TestCircles()
    {
        Circle a;
        a.radius = 0.5f;
        Circle b;
        b.radius = 0.25f;
        const Manifold overlap = JBro::Physics2D::CollideCircles(a, At(0, 0), b, At(0.7f, 0));
        Check(overlap.count == 1, "overlapping circles touch at one point");
        Check(NearVector(overlap.normal, { 1, 0 }, 1.0e-6f), "the normal runs from A's center to B's");
        Check(Near(overlap.points[0].separation, -0.05f, 1.0e-6f), "0.05 deep");
        Check(NearVector(overlap.points[0].point, { 0.475f, 0 }, 1.0e-6f), "the point is between the two surfaces");

        const Manifold same = JBro::Physics2D::CollideCircles(a, At(1, 1), b, At(1, 1));
        Check(same.count == 1 && Near(same.normal.x * same.normal.x + same.normal.y * same.normal.y, 1.0f, 1.0e-6f),
            "concentric circles still get a unit normal, not NaN");

        const Manifold apart = JBro::Physics2D::CollideCircles(a, At(0, 0), b, At(2, 0));
        Check(apart.count == 0, "far circles do not touch");
    }

    // **떨어져 있어도 가까우면 미리 만든다.** 틈이 SpeculativeDistance 안이면 양의 separation 으로 들어오고, 넘으면 없다.
    void TestSpeculativeContacts()
    {
        const ConvexPolygon box = MakeBox(0.5f, 0.5f);
        const ConvexPolygon floor = MakeBox(5.0f, 0.5f);
        const float gap = 0.5f * JBro::Physics2D::SpeculativeDistance;
        const Manifold near = JBro::Physics2D::CollidePolygons(floor, At(0, -0.5f), box, At(0, 0.5f + gap));
        Check(near.count == 2, "a box hovering within the speculative distance already has contacts");
        Check(Near(near.points[0].separation, gap, 1.0e-5f), "with the gap as positive separation");

        const Manifold far = JBro::Physics2D::CollidePolygons(
            floor, At(0, -0.5f), box, At(0, 0.5f + 2.0f * JBro::Physics2D::SpeculativeDistance));
        Check(far.count == 0, "beyond it there is nothing");

        Circle ball;
        ball.radius = 0.5f;
        const Manifold ballNear = JBro::Physics2D::CollidePolygonAndCircle(floor, At(0, -0.5f), ball, At(0, 0.5f + gap));
        Check(ballNear.count == 1 && Near(ballNear.points[0].separation, gap, 1.0e-5f),
            "a hovering circle gets one speculative point");
    }

    // **접촉 번호는 같은 두 면이 맞닿아 있는 한 그대로다.** 번호가 바뀌면 누적 임펄스가 끊겨 쌓인 상자가 흔들린다.
    void TestContactIdsStayWhileTheSameFacesTouch()
    {
        const ConvexPolygon box = MakeBox(0.5f, 0.5f);
        const ConvexPolygon floor = MakeBox(5.0f, 0.5f);
        const Manifold first = JBro::Physics2D::CollidePolygons(floor, At(0, -0.5f), box, At(0, 0.49f));
        const Manifold moved = JBro::Physics2D::CollidePolygons(floor, At(0, -0.5f), box, At(0.01f, 0.485f, 0.002f));
        Check(first.count == 2 && moved.count == 2, "both steps rest on two points");
        Check(first.points[0].id != first.points[1].id, "the two points have different ids");
        const bool sameOrder = first.points[0].id == moved.points[0].id && first.points[1].id == moved.points[1].id;
        const bool swapped = first.points[0].id == moved.points[1].id && first.points[1].id == moved.points[0].id;
        Check(sameOrder || swapped, "a small slide keeps both ids");
    }

    // **45 도 돌린 상자는 모서리 하나로 바닥에 닿는다.**
    void TestARotatedBoxTouchesWithItsCorner()
    {
        const ConvexPolygon box = MakeBox(0.5f, 0.5f);
        const ConvexPolygon floor = MakeBox(5.0f, 0.5f);
        const float halfDiagonal = std::sqrt(0.5f);
        const Manifold corner = JBro::Physics2D::CollidePolygons(
            floor, At(0, -0.5f), box, At(0, halfDiagonal - 0.01f, 0.78539816f));
        Check(corner.count == 1, "one corner, one point");
        Check(NearVector(corner.normal, { 0, 1 }, 1.0e-4f), "pushed straight up");
        Check(Near(corner.points[0].separation, -0.01f, 1.0e-4f), "0.01 deep");
        Check(Near(corner.points[0].point.x, 0.0f, 1.0e-4f), "under the box's center");
    }

    void TestBounds()
    {
        const Rect polygon = JBro::Physics2D::ComputePolygonBounds(MakeBox(1, 0.5f), At(2, 3, 1.5707963f));
        Check(NearVector(polygon.min, { 1.5f, 2.0f }, 1.0e-5f) && NearVector(polygon.max, { 2.5f, 4.0f }, 1.0e-5f),
            "a quarter-turned box swaps its extents");
        Circle circle;
        circle.center = { 1, 0 };
        circle.radius = 0.5f;
        const Rect round = JBro::Physics2D::ComputeCircleBounds(circle, At(0, 0, 1.5707963f));
        Check(NearVector(round.min, { -0.5f, 0.5f }, 1.0e-5f) && NearVector(round.max, { 0.5f, 1.5f }, 1.0e-5f),
            "a circle's offset center turns with the pose");
    }

    // **브로드페이즈는 모든 쌍 비교와 같은 답을 같은 순서로 낸다.**
    void TestSweepAndPruneMatchesBruteForce()
    {
        std::uint32_t state = 0x9E3779B9u;
        const auto next = [&state]()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
        };

        JBro::Physics2D::SweepAndPrune broadPhase;
        Array<ProxyPair> pairs;
        for (int round = 0; round < 20; ++round)
        {
            Array<Rect> boxes;
            const int count = 1 + static_cast<int>(next() * 60.0f);
            for (int i = 0; i < count; ++i)
            {
                const Vec2 min = { next() * 20.0f, next() * 20.0f };
                // 한 줄로 늘어선 경우(같은 x)도 섞는다.
                const float x = (i % 5 == 0) ? 3.0f : min.x;
                boxes.Add({ { x, min.y }, { x + next() * 3.0f, min.y + next() * 3.0f } });
            }
            boxes.Add({ { std::numeric_limits<float>::quiet_NaN(), 0 }, { 1, 1 } });

            broadPhase.FindPairs(boxes.View(), pairs);

            Array<ProxyPair> expected;
            for (std::uint32_t i = 0; i < boxes.Size(); ++i)
            {
                for (std::uint32_t j = i + 1; j < boxes.Size(); ++j)
                {
                    const Rect& a = boxes[i];
                    const Rect& b = boxes[j];
                    if (a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y)
                    {
                        expected.Add({ i, j });
                    }
                }
            }
            Check(pairs.Size() == expected.Size(), "the sweep finds every overlapping pair and no other");
            for (std::size_t k = 0; k < pairs.Size(); ++k)
            {
                Check(pairs[k].first == expected[k].first && pairs[k].second == expected[k].second,
                    "in the same (first, second) order");
            }
        }

        broadPhase.FindPairs(ArrayView<const Rect>(), pairs);
        Check(pairs.IsEmpty(), "no boxes, no pairs");

        // 경계에서 맞닿은 상자도 쌍이다. 나란히 놓인 바닥 타일 위를 지나가는 물체가 이음매에서 빠지지 않아야 한다.
        const Rect touching[] = { { { 0, 0 }, { 1, 1 } }, { { 1, 0 }, { 2, 1 } }, { { 0, 1 }, { 1, 2 } } };
        broadPhase.FindPairs(ArrayView<const Rect>(touching), pairs);
        Check(pairs.Size() == 3, "boxes sharing an edge or a corner pair up");
    }

    // 조각 전부에 쏘아 가장 가까운 것을 고른다. 어댑터의 Raycast 가 하는 일과 같다.
    bool RaycastPieces(const Array<ConvexPolygon>& pieces, Vec2 origin, Vec2 direction, float maxDistance,
        float& distance, Vec2& normal)
    {
        bool hit = false;
        for (const ConvexPolygon& piece : pieces)
        {
            float candidate = 0.0f;
            Vec2 candidateNormal;
            if (JBro::Physics2D::RaycastPolygon(piece, At(0, 0), origin, direction, maxDistance, candidate, candidateNormal)
                && (false == hit || candidate < distance))
            {
                hit = true;
                distance = candidate;
                normal = candidateNormal;
            }
        }
        return hit;
    }

    // **반직선.** 상자의 가까운 면, 돌린 상자, 원, 출발점이 안인 경우, 거리가 모자란 경우.
    // U 의 홈으로 내리꽂은 반직선은 홈 바닥에 맞는다 - 통짜 외곽선의 볼록 껍질로 재면 홈 입구(y = 3)에서 맞았다고 나온다.
    void TestRaycasts()
    {
        const ConvexPolygon box = MakeBox(1.0f, 1.0f);
        float distance = 0.0f;
        Vec2 normal;
        Check(JBro::Physics2D::RaycastPolygon(box, At(3, 0), { 0, 0 }, { 1, 0 }, 10.0f, distance, normal),
            "a ray along x hits a box ahead");
        Check(Near(distance, 2.0f, 1.0e-5f) && NearVector(normal, { -1, 0 }, 1.0e-5f), "on its near face");
        Check(false == JBro::Physics2D::RaycastPolygon(box, At(3, 0), { 0, 0 }, { 1, 0 }, 1.5f, distance, normal),
            "a ray that stops short misses");
        Check(false == JBro::Physics2D::RaycastPolygon(box, At(3, 0), { 0, 0 }, { -1, 0 }, 10.0f, distance, normal),
            "a ray pointing away misses");
        Check(false == JBro::Physics2D::RaycastPolygon(box, At(3, 5), { 0, 0 }, { 1, 0 }, 10.0f, distance, normal),
            "a ray passing beside misses");

        Check(JBro::Physics2D::RaycastPolygon(box, At(3, 0, 0.78539816f), { 0, 0 }, { 1, 0 }, 10.0f, distance, normal),
            "a ray hits a diamond");
        Check(Near(distance, 3.0f - std::sqrt(2.0f), 1.0e-4f), "at its near corner");

        Check(JBro::Physics2D::RaycastPolygon(box, At(0, 0), { 0.5f, 0 }, { 1, 0 }, 10.0f, distance, normal),
            "a ray starting inside reports a hit");
        Check(distance == 0.0f && NearVector(normal, { -1, 0 }, 0.0f), "at distance zero against its direction");

        const Array<Vec2> u = {
            { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        const Array<ConvexPolygon> pieces = Decompose(u);
        Check(RaycastPieces(pieces, { 1.5f, 5.0f }, { 0, -1 }, 10.0f, distance, normal),
            "a ray dropped into the notch of a U hits something");
        Check(Near(distance, 4.0f, 1.0e-4f) && NearVector(normal, { 0, 1 }, 1.0e-5f), "the notch floor, not its mouth");
        Check(RaycastPieces(pieces, { 1.5f, 2.0f }, { 1, 0 }, 10.0f, distance, normal)
            && Near(distance, 0.5f, 1.0e-4f) && NearVector(normal, { -1, 0 }, 1.0e-5f),
            "from inside the notch a ray hits the inner wall");

        Circle ball;
        ball.center = { 0, 1 };
        ball.radius = 0.5f;
        Check(JBro::Physics2D::RaycastCircle(ball, At(3, 0), { 0, 1 }, { 1, 0 }, 10.0f, distance, normal),
            "a ray hits a circle with an offset center");
        Check(Near(distance, 2.5f, 1.0e-5f) && NearVector(normal, { -1, 0 }, 1.0e-5f), "on its near side");
        Check(false == JBro::Physics2D::RaycastCircle(ball, At(3, 0), { 0, 0 }, { 1, 0 }, 10.0f, distance, normal),
            "and misses when the ray passes below it");
        Check(false == JBro::Physics2D::RaycastCircle(ball, At(3, 0), { 0, 1 }, { 1, 0 }, 2.0f, distance, normal),
            "or stops short");
        Check(JBro::Physics2D::RaycastCircle(ball, At(3, 0), { 3, 1 }, { 1, 0 }, 10.0f, distance, normal)
            && distance == 0.0f, "a ray starting inside a circle reports distance zero");
    }

    void TestOverlaps()
    {
        const ConvexPolygon box = MakeBox(1.0f, 1.0f);
        Check(JBro::Physics2D::OverlapPolygons(box, At(0, 0), box, At(1.5f, 0)), "overlapping boxes overlap");
        Check(JBro::Physics2D::OverlapPolygons(box, At(0, 0), box, At(2.0f, 0)), "touching boxes count");
        Check(false == JBro::Physics2D::OverlapPolygons(box, At(0, 0), box, At(2.1f, 0)), "apart boxes do not");
        // 축 정렬 상자로는 겹치지만 돌린 상자의 축이 가르는 경우.
        Check(false == JBro::Physics2D::OverlapPolygons(box, At(0, 0), box, At(2.0f, 2.0f, 0.78539816f)),
            "a diamond beside a corner is apart even though their bounds overlap");

        Circle ball;
        ball.radius = 0.5f;
        Check(JBro::Physics2D::OverlapPolygonAndCircle(box, At(0, 0), ball, At(1.4f, 0)), "a ball against a face");
        Check(false == JBro::Physics2D::OverlapPolygonAndCircle(box, At(0, 0), ball, At(1.4f, 1.4f)),
            "a ball near a corner but outside its reach does not overlap");
        Check(JBro::Physics2D::OverlapPolygonAndCircle(box, At(0, 0), ball, At(0, 0)), "a ball inside does");
    }
}

int RunPhysics2DCollisionTests()
{
    TestABoxInTheNotchOfAUIsPushedOutOfTheWall();
    TestSwappingTheShapesFlipsOnlyTheNormal();
    TestACircleInTheInnerCornerOfAnLGetsBothWalls();
    TestCircleAgainstACornerAndFromInside();
    TestRaycasts();
    TestOverlaps();
    TestCircles();
    TestSpeculativeContacts();
    TestContactIdsStayWhileTheSameFacesTouch();
    TestARotatedBoxTouchesWithItsCorner();
    TestBounds();
    TestSweepAndPruneMatchesBruteForce();
    std::cout << "Physics2D collision tests passed.\n";
    return 0;
}
