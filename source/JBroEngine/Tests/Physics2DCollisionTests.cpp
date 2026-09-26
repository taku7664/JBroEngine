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

    // **점·원 겹침.** 경계 위도 든다.
    void TestPointsAndCircles()
    {
        const ConvexPolygon box = MakeBox(1.0f, 1.0f);
        Check(JBro::Physics2D::ContainsPoint(box, At(3, 0), { 3.5f, 0.5f }), "a point inside a box");
        Check(JBro::Physics2D::ContainsPoint(box, At(3, 0), { 4.0f, 0.0f }), "and one on its face");
        Check(false == JBro::Physics2D::ContainsPoint(box, At(3, 0), { 4.1f, 0.0f }), "but not one past it");
        Check(false == JBro::Physics2D::ContainsPoint(box, At(0, 0, 0.78539816f), { 0.9f, 0.9f }),
            "the corner of the unrotated box is outside the diamond");

        Circle ball;
        ball.center = { 1, 0 };
        ball.radius = 0.5f;
        Check(JBro::Physics2D::ContainsPoint(ball, At(0, 0), { 1.4f, 0.0f }), "a point inside an offset circle");
        Check(false == JBro::Physics2D::ContainsPoint(ball, At(0, 0), { 0.4f, 0.0f }), "and one outside it");
        Circle other;
        other.radius = 0.5f;
        Check(JBro::Physics2D::OverlapCircles(ball, At(0, 0), other, At(2.0f, 0)), "touching circles overlap");
        Check(false == JBro::Physics2D::OverlapCircles(ball, At(0, 0), other, At(2.1f, 0)), "apart ones do not");
    }

    // **스윕.** 모양을 밀면서 처음 닿는 거리와 상대 표면의 법선. 값은 손으로 푼 것이다.
    void TestSweeps()
    {
        const ConvexPolygon target = MakeBox(1.0f, 1.0f);
        float distance = 0.0f;
        Vec2 normal;

        // 원: 면, 모서리, 출발부터 겹침, 빗나감, 거리 모자람, 원 대 원.
        Check(JBro::Physics2D::CastCircle({ 0, 0 }, 0.5f, { 1, 0 }, 10.0f, target, At(3, 0), distance, normal),
            "a circle swept at a box hits it");
        Check(Near(distance, 1.5f, 1.0e-4f) && NearVector(normal, { -1, 0 }, 1.0e-5f),
            "on its near face, a radius short of it");
        Check(JBro::Physics2D::CastCircle({ 0, 1.3f }, 0.5f, { 1, 0 }, 10.0f, target, At(3, 0), distance, normal),
            "a circle passing the top corner clips it");
        Check(Near(distance, 1.6f, 1.0e-4f) && NearVector(normal, { -0.8f, 0.6f }, 1.0e-4f),
            "rounding the corner: 2 - sqrt(0.25 - 0.09), normal from the corner to the center");
        Check(false == JBro::Physics2D::CastCircle({ 0, 1.6f }, 0.5f, { 1, 0 }, 10.0f, target, At(3, 0), distance, normal),
            "a circle passing above the box misses");
        Check(false == JBro::Physics2D::CastCircle({ 0, 0 }, 0.5f, { 1, 0 }, 1.0f, target, At(3, 0), distance, normal),
            "and one that stops short misses");
        Check(JBro::Physics2D::CastCircle({ 2.2f, 0 }, 0.5f, { 1, 0 }, 10.0f, target, At(3, 0), distance, normal)
            && distance == 0.0f && NearVector(normal, { -1, 0 }, 0.0f),
            "a circle that starts inside reports distance zero against its direction");
        Circle round;
        round.radius = 1.0f;
        Check(JBro::Physics2D::CastCircle({ 0, 0 }, 0.5f, { 1, 0 }, 10.0f, round, At(4, 0), distance, normal)
            && Near(distance, 2.5f, 1.0e-4f) && NearVector(normal, { -1, 0 }, 1.0e-5f),
            "a circle swept at a circle stops when the radii touch");

        // 상자: 면, 돌린 상자의 모서리, 원, 출발부터 겹침, 빗나감.
        const ConvexPolygon mover = MakeBox(0.5f, 0.5f);
        Check(JBro::Physics2D::CastPolygon(mover, At(0, 0), { 1, 0 }, 10.0f, target, At(3, 0), distance, normal),
            "a box swept at a box hits it");
        Check(Near(distance, 1.5f, 1.0e-4f) && NearVector(normal, { -1, 0 }, 1.0e-5f), "face to face");
        Check(JBro::Physics2D::CastPolygon(mover, At(0, 0), { 1, 0 }, 10.0f, target, At(3, 0, 0.78539816f), distance, normal),
            "a box swept at a diamond hits it");
        Check(Near(distance, 3.0f - std::sqrt(2.0f) - 0.5f, 1.0e-4f), "at the diamond's near corner");
        Check(JBro::Physics2D::CastPolygon(mover, At(0, 0), { 1, 0 }, 10.0f, round, At(3, 0), distance, normal)
            && Near(distance, 1.5f, 1.0e-4f) && NearVector(normal, { -1, 0 }, 1.0e-5f),
            "a box swept at a circle stops at the circle, with the circle's normal");
        Check(JBro::Physics2D::CastPolygon(mover, At(1.8f, 0), { 1, 0 }, 10.0f, target, At(3, 0), distance, normal)
            && distance == 0.0f && NearVector(normal, { -1, 0 }, 0.0f),
            "a box that starts overlapping reports distance zero");
        Check(false == JBro::Physics2D::CastPolygon(mover, At(0, 2), { 1, 0 }, 10.0f, target, At(3, 0), distance, normal),
            "a box passing above misses");
        Check(false == JBro::Physics2D::CastPolygon(mover, At(0, 0), { -1, 0 }, 10.0f, target, At(3, 0), distance, normal),
            "and one swept away from the target misses");

        // U 의 홈으로 내리꽂은 원은 홈 바닥에 선다 - 조각마다 쏘아 가장 가까운 것이다.
        const Array<Vec2> u = {
            { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        const Array<ConvexPolygon> pieces = Decompose(u);
        bool hit = false;
        float closest = 100.0f;
        Vec2 closestNormal;
        for (const ConvexPolygon& piece : pieces)
        {
            if (JBro::Physics2D::CastCircle({ 1.5f, 5.0f }, 0.3f, { 0, -1 }, 10.0f, piece, At(0, 0), distance, normal)
                && distance < closest)
            {
                hit = true;
                closest = distance;
                closestNormal = normal;
            }
        }
        Check(hit && Near(closest, 3.7f, 1.0e-4f) && NearVector(closestNormal, { 0, 1 }, 1.0e-5f),
            "a ball dropped into the notch of a U lands on the notch floor, 5 - 1 - 0.3 down");
    }

    // **캡슐(physics-plan §4 의 7).** 가로 캡슐: 코어 (-1, 0)-(1, 0), 반지름 0.5. 값은 손으로 푼 것이다.
    ConvexPolygon MakeCapsule(Vec2 a, Vec2 b, float radius)
    {
        ConvexPolygon capsule;
        capsule.points[0] = a;
        capsule.points[1] = b;
        capsule.count = 2;
        capsule.radius = radius;
        return capsule;
    }

    void TestCapsuleShapeAndMass()
    {
        const ConvexPolygon lying = JBro::Physics2D::MakeCapsuleInBox({ 1, 2 }, { 2, 0.5f });
        Check(lying.count == 2 && Near(lying.radius, 0.5f, 0.0f)
            && NearVector(lying.points[0], { -0.5f, 2 }, 0.0f) && NearVector(lying.points[1], { 2.5f, 2 }, 0.0f),
            "a wide box holds a lying capsule, its radius half the short side");
        const ConvexPolygon standing = JBro::Physics2D::MakeCapsuleInBox({}, { -0.5f, 1.5f });
        Check(Near(standing.radius, 0.5f, 0.0f) && NearVector(standing.points[0], { 0, -1 }, 0.0f)
            && NearVector(standing.points[1], { 0, 1 }, 0.0f), "a tall one a standing capsule, even from a flipped size");
        const ConvexPolygon round = JBro::Physics2D::MakeCapsuleInBox({}, { 0.5f, 0.5f });
        Check(NearVector(round.points[0], round.points[1], 0.0f), "a square one a circle");

        // 조각 질량이 캡슐 공식과 촘촘한 다각형 외곽선의 값에 맞는다(서로 독립인 두 계산).
        const JBro::Physics2D::MassData mass = JBro::Physics2D::ComputePolygonMass(MakeCapsule({ -1, 0 }, { 1, 0 }, 0.5f), 1.0f);
        Check(Near(mass.mass, 2.0f + 3.14159265f * 0.25f, 1.0e-5f) && NearVector(mass.center, {}, 1.0e-6f),
            "a two-point piece weighs as a capsule: a 2 x 1 middle and a circle");
        Array<Vec2> outline;
        constexpr int Segments = 2000;
        for (int end = 0; end < 2; ++end)
        {
            const float cx = end == 0 ? 1.0f : -1.0f;
            const float start = end == 0 ? -1.5707963f : 1.5707963f;
            for (int k = 0; k <= Segments; ++k)
            {
                const float turn = start + 3.14159265f * static_cast<float>(k) / static_cast<float>(Segments);
                outline.Add({ cx + 0.5f * std::cos(turn), 0.5f * std::sin(turn) });
            }
        }
        const JBro::Physics2D::MassData traced = JBro::Physics2D::ComputeOutlineMass(outline.View(), 1.0f);
        Check(Near(mass.mass, traced.mass, 1.0e-4f) && Near(mass.inertia, traced.inertia, 1.0e-3f),
            "and matches the mass and inertia of a traced outline");
    }

    void TestCapsuleContacts()
    {
        const ConvexPolygon capsule = MakeCapsule({ -1, 0 }, { 1, 0 }, 0.5f);
        Circle ball;
        ball.radius = 0.5f;

        Manifold manifold = JBro::Physics2D::CollidePolygonAndCircle(capsule, At(0, 0), ball, At(0, 1.2f));
        Check(manifold.count == 0, "a ball 0.2 above the capsule's side does not touch");
        manifold = JBro::Physics2D::CollidePolygonAndCircle(capsule, At(0, 0), ball, At(0, 0.99f));
        Check(manifold.count == 1 && Near(manifold.points[0].separation, -0.01f, 1.0e-5f)
            && NearVector(manifold.normal, { 0, 1 }, 1.0e-6f) && Near(manifold.points[0].point.y, 0.495f, 1.0e-5f),
            "one 0.01 into it touches its side, the point between the two surfaces");
        manifold = JBro::Physics2D::CollidePolygonAndCircle(capsule, At(0, 0), ball, At(1.99f, 0));
        Check(manifold.count == 1 && Near(manifold.points[0].separation, -0.01f, 1.0e-5f)
            && NearVector(manifold.normal, { 1, 0 }, 1.0e-6f) && Near(manifold.points[0].point.x, 1.495f, 1.0e-5f),
            "and its round end the same way");

        // 상자 위에 누운 캡슐은 두 점으로 선다.
        const ConvexPolygon ground = MakeBox(1.0f, 1.0f);
        manifold = JBro::Physics2D::CollidePolygons(ground, At(0, -1), capsule, At(0, 0.49f));
        Check(manifold.count == 2 && NearVector(manifold.normal, { 0, 1 }, 1.0e-6f)
            && Near(manifold.points[0].separation, -0.01f, 1.0e-5f) && Near(manifold.points[1].separation, -0.01f, 1.0e-5f)
            && Near(manifold.points[0].point.y, -0.005f, 1.0e-5f),
            "a capsule lying on a box rests on two points, between the surfaces");

        // **둥근 끝은 모서리 옆의 틈을 닿은 것으로 보지 않는다.** 면 법선만 보면 x 로 0.1 겹친 것처럼 보이지만
        // 코어 끝 (1.3, 1.3) 과 상자 모서리 (1, 1) 사이는 0.3√2 = 0.424 로 반지름 0.4 보다 멀다.
        const ConvexPolygon standing = MakeCapsule({ 0, 0 }, { 0, 1.7f }, 0.4f);
        manifold = JBro::Physics2D::CollidePolygons(ground, At(0, 0), standing, At(1.3f, 1.3f));
        Check(manifold.count == 0, "a capsule end beside a box corner, 0.024 away, does not touch");
        manifold = JBro::Physics2D::CollidePolygons(ground, At(0, 0), standing, At(1.25f, 1.25f));
        const float diagonal = std::sqrt(0.5f);
        Check(manifold.count == 1 && NearVector(manifold.normal, { diagonal, diagonal }, 1.0e-5f)
            && Near(manifold.points[0].separation, 0.25f * std::sqrt(2.0f) - 0.4f, 1.0e-5f),
            "moved in, it touches the corner once, pushed out along the diagonal");
        manifold = JBro::Physics2D::CollidePolygons(standing, At(1.25f, 1.25f), ground, At(0, 0));
        Check(manifold.count == 1 && NearVector(manifold.normal, { -diagonal, -diagonal }, 1.0e-5f),
            "and swapping the shapes flips only the normal");

        // 두 캡슐이 끝끼리 닿는다.
        manifold = JBro::Physics2D::CollidePolygons(capsule, At(0, 0), capsule, At(2.99f, 0));
        Check(manifold.count >= 1 && NearVector(manifold.normal, { 1, 0 }, 1.0e-5f)
            && Near(manifold.points[0].separation, -0.01f, 1.0e-5f), "two capsules touch end to end");
        manifold = JBro::Physics2D::CollidePolygons(capsule, At(0, 0), capsule, At(0.5f, 0.99f));
        Check(manifold.count == 2 && NearVector(manifold.normal, { 0, 1 }, 1.0e-6f)
            && Near(manifold.points[0].separation, -0.01f, 1.0e-5f) && Near(manifold.points[1].separation, -0.01f, 1.0e-5f)
            && Near(std::fmin(manifold.points[0].point.x, manifold.points[1].point.x), -0.5f, 1.0e-5f)
            && Near(std::fmax(manifold.points[0].point.x, manifold.points[1].point.x), 1.0f, 1.0e-5f),
            "one lying on another rests on two points, where they overlap");
    }

    void TestCapsuleQueries()
    {
        const ConvexPolygon capsule = MakeCapsule({ -1, 0 }, { 1, 0 }, 0.5f);
        const Rect bounds = JBro::Physics2D::ComputePolygonBounds(capsule, At(0, 0));
        Check(NearVector(bounds.min, { -1.5f, -0.5f }, 0.0f) && NearVector(bounds.max, { 1.5f, 0.5f }, 0.0f),
            "the bounds include the thickness");

        float distance = 0.0f;
        Vec2 normal;
        Check(JBro::Physics2D::RaycastPolygon(capsule, At(0, 0), { 0, 5 }, { 0, -1 }, 10, distance, normal)
            && Near(distance, 4.5f, 1.0e-5f) && NearVector(normal, { 0, 1 }, 1.0e-6f), "a ray down hits the side");
        Check(JBro::Physics2D::RaycastPolygon(capsule, At(0, 0), { 5, 0.4f }, { -1, 0 }, 10, distance, normal)
            && Near(distance, 3.7f, 1.0e-5f) && NearVector(normal, { 0.6f, 0.8f }, 1.0e-5f),
            "a ray along x at 0.4 hits the round end at x = 1.3");
        Check(false == JBro::Physics2D::RaycastPolygon(capsule, At(0, 0), { 5, 0.6f }, { -1, 0 }, 10, distance, normal),
            "one above the thickness misses");

        Check(JBro::Physics2D::ContainsPoint(capsule, At(0, 0), { 1.4f, 0 }), "a point in the round end is in");
        Check(false == JBro::Physics2D::ContainsPoint(capsule, At(0, 0), { 1.4f, 0.4f }),
            "one in the corner of its bounds is not");
        Check(JBro::Physics2D::OverlapPolygons(MakeBox(0.15f, 0.15f), At(1.45f, 0.45f), capsule, At(0, 0)),
            "a box reaching within the radius of the core's end overlaps");
        Check(false == JBro::Physics2D::OverlapPolygons(MakeBox(0.1f, 0.1f), At(1.5f, 0.5f), capsule, At(0, 0)),
            "one in the corner beside the round end does not");

        Check(JBro::Physics2D::CastCircle({ 5, 0.4f }, 0.25f, { -1, 0 }, 10, capsule, At(0, 0), distance, normal)
            && Near(distance, 4.0f - std::sqrt(0.75f * 0.75f - 0.16f), 1.0e-4f),
            "a ball swept at the round end stops a combined radius from the core's end");
        Check(JBro::Physics2D::CastPolygon(MakeBox(0.5f, 0.5f), At(5, 0), { -1, 0 }, 10, capsule, At(0, 0), distance, normal)
            && Near(distance, 3.0f, 1.0e-4f) && NearVector(normal, { 1, 0 }, 1.0e-4f),
            "a box swept at it meets the tip of the round end");
        const ConvexPolygon upright = MakeCapsule({ 0, -0.5f }, { 0, 0.5f }, 0.25f);
        Check(JBro::Physics2D::CastPolygon(upright, At(5, 0), { -1, 0 }, 10, capsule, At(0, 0), distance, normal)
            && Near(distance, 3.25f, 1.0e-4f) && NearVector(normal, { 1, 0 }, 1.0e-4f),
            "a standing capsule swept at it stops both radii from the core");
        Check(JBro::Physics2D::CastPolygon(upright, At(1.6f, 0), { -1, 0 }, 10, capsule, At(0, 0), distance, normal)
            && distance == 0.0f, "one that starts touching reports zero");
    }

    // **체인 선분은 이음매에서 옆으로 걸리지 않는다(D-228).** 체인 (-5,0)-(0,0)-(5,0). 상자(반폭 0.5)가 0.01 박힌 채 오른쪽 끝이 이음매를 0.005
    // 넘었다. 이웃을 모르는 선분 (0,0)-(5,0) 은 상자의 옆면을 기준면으로 골라 가로로 민다(유령 충돌). 체인 선분은 평평한 꼭짓점이라 세로로 민다.
    void TestChainSegmentsHaveNoGhostCollisions()
    {
        using JBro::Physics2D::ChainSegment;
        ChainSegment right;
        right.p1 = { 0, 0 };
        right.p2 = { 5, 0 };
        right.previous = { -5, 0 };
        right.hasPrevious = true;
        ChainSegment left;
        left.p1 = { -5, 0 };
        left.p2 = { 0, 0 };
        left.next = { 5, 0 };
        left.hasNext = true;
        const ConvexPolygon box = MakeBox(0.5f, 0.5f);
        const Pose boxPose = At(-0.495f, 0.49f);

        ConvexPolygon bare;
        bare.points[0] = right.p1;
        bare.points[1] = right.p2;
        bare.count = 2;
        const Manifold ghost = JBro::Physics2D::CollidePolygons(bare, At(0, 0), box, boxPose);
        Check(ghost.count > 0 && std::fabs(ghost.normal.x) > 0.9f, "a bare segment snags the box at the seam with a sideways normal");

        const Manifold smooth = JBro::Physics2D::CollideChainSegmentAndPolygon(right, At(0, 0), box, boxPose);
        Check(smooth.count > 0 && NearVector(smooth.normal, { 0, 1 }, 1.0e-5f)
            && Near(smooth.points[0].separation, -0.01f, 1.0e-4f), "a chain segment pushes it straight up instead");
        const Manifold beside = JBro::Physics2D::CollideChainSegmentAndPolygon(left, At(0, 0), box, boxPose);
        Check(beside.count == 2 && NearVector(beside.normal, { 0, 1 }, 1.0e-5f), "and so does its neighbor, on two points");

        // 원: 중심이 이음매 바로 앞(선분 범위 밖)이면 오른쪽 선분은 내놓고 왼쪽이 면으로 맡는다.
        Circle ball;
        ball.radius = 0.5f;
        const Manifold ballRight = JBro::Physics2D::CollideChainSegmentAndCircle(right, At(0, 0), ball, At(-0.1f, 0.49f));
        const Manifold ballLeft = JBro::Physics2D::CollideChainSegmentAndCircle(left, At(0, 0), ball, At(-0.1f, 0.49f));
        Check(ballRight.count == 0 && ballLeft.count == 1 && NearVector(ballLeft.normal, { 0, 1 }, 1.0e-5f),
            "a ball just before the seam is held up by the left segment's face alone");
        // 체인의 끝(이웃 없음)은 모서리를 그대로 받는다.
        ChainSegment lone = right;
        lone.hasPrevious = false;
        const Manifold corner = JBro::Physics2D::CollideChainSegmentAndCircle(lone, At(0, 0), ball, At(-0.3f, 0.3f));
        Check(corner.count == 1 && corner.normal.x < -0.5f, "at a free end the ball rounds the corner");
    }

    // **볼록한 꼭짓점(D-228).** 평평한 선분 (-5,0)-(0,0) 뒤에 수직으로 내려가는 선분 (0,0)-(0,-5). 꼭짓점을 둘러싼 원은 끝으로 가진
    // 앞 선분이 맡고(대각 법선), 뒤 선분은 그 꼭짓점을 내놓는다. 두 면 사이 밖의 법선은 버린다.
    void TestAConvexChainCornerIsOwnedOnce()
    {
        using JBro::Physics2D::ChainSegment;
        ChainSegment top;
        top.p1 = { -5, 0 };
        top.p2 = { 0, 0 };
        top.next = { 0, -5 };
        top.hasNext = true;
        ChainSegment wall;
        wall.p1 = { 0, 0 };
        wall.p2 = { 0, -5 };
        wall.previous = { -5, 0 };
        wall.hasPrevious = true;
        Circle ball;
        ball.radius = 0.5f;
        const Pose around = At(0.3f, 0.3f);
        const Manifold byTop = JBro::Physics2D::CollideChainSegmentAndCircle(top, At(0, 0), ball, around);
        const Manifold byWall = JBro::Physics2D::CollideChainSegmentAndCircle(wall, At(0, 0), ball, around);
        const float d = std::sqrt(0.5f);
        Check(byTop.count == 1 && NearVector(byTop.normal, { d, d }, 1.0e-4f), "the flat segment owns the corner, pushing along the diagonal");
        Check(byWall.count == 0, "and the wall below lets it go, so the corner pushes once");
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
    TestPointsAndCircles();
    TestSweeps();
    TestCircles();
    TestSpeculativeContacts();
    TestContactIdsStayWhileTheSameFacesTouch();
    TestARotatedBoxTouchesWithItsCorner();
    TestBounds();
    TestSweepAndPruneMatchesBruteForce();
    TestCapsuleShapeAndMass();
    TestCapsuleContacts();
    TestCapsuleQueries();
    TestChainSegmentsHaveNoGhostCollisions();
    TestAConvexChainCornerIsOwnedOnce();
    std::cout << "Physics2D collision tests passed.\n";
    return 0;
}
