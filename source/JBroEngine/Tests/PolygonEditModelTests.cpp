#include <JBro/Editor/Gizmo/PolygonEditModel.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

// 캔버스 뷰 폴리곤 편집의 모델 테스트(physics-plan §4 의 5 단계). 화면 없이 잰다.
namespace
{
    using JBro::Array;
    using JBro::Vec2;
    using JBro::PolygonEditModel::HitKind;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // 화면의 100 픽셀 정사각형.
    Array<Vec2> Square()
    {
        return { { 100, 100 }, { 200, 100 }, { 200, 200 }, { 100, 200 } };
    }

    void TestPickPrefersAVertexOverItsEdges()
    {
        const Array<Vec2> screen = Square();
        auto hit = JBro::PolygonEditModel::Pick(screen.View(), { 203, 98 });
        Check(hit.kind == HitKind::Vertex && hit.index == 1,
            "next to a corner the vertex wins, though two edges are just as close");

        hit = JBro::PolygonEditModel::Pick(screen.View(), { 150, 104 });
        Check(hit.kind == HitKind::Edge && hit.index == 0, "near the bottom edge its first vertex names the edge");
        Check(hit.point.x == 150.0f && hit.point.y == 100.0f, "and the insert point is the foot on the edge");

        hit = JBro::PolygonEditModel::Pick(screen.View(), { 150, 150 });
        Check(hit.kind == HitKind::None, "the middle of the shape is neither");

        hit = JBro::PolygonEditModel::Pick(screen.View(), { 104, 160 });
        Check(hit.kind == HitKind::Edge && hit.index == 3, "the closing edge from the last vertex back to the first counts");

        hit = JBro::PolygonEditModel::Pick(screen.View(), { 100 + JBro::PolygonEditModel::VertexPickRadius + 1, 100 });
        Check(hit.kind == HitKind::Edge, "just past the vertex radius on its edge the edge takes over");

        // 두 버텍스가 모두 닿으면 더 가까운 쪽이다.
        const Array<Vec2> tight = { { 0, 0 }, { 6, 0 }, { 3, 50 } };
        hit = JBro::PolygonEditModel::Pick(tight.View(), { 5, 0 });
        Check(hit.kind == HitKind::Vertex && hit.index == 1, "of two vertices in reach the nearer one is taken");
    }

    void TestSeedStartsFromWhatIsDrawn()
    {
        JBro::Component::Collider2D collider;
        collider.size = { 2, 4 };
        Array<Vec2> seed;
        JBro::PolygonEditModel::SeedPoints(collider, seed);
        Check(seed.Size() == 4, "an empty polygon starts from the four corners of its size box");
        Check(seed[0].x == -1.0f && seed[0].y == -2.0f && seed[2].x == 1.0f && seed[2].y == 2.0f,
            "centered on the object, counter-clockwise from the bottom left");

        collider.points = { { 0, 0 }, { 3, 0 }, { 0, 3 } };
        JBro::PolygonEditModel::SeedPoints(collider, seed);
        Check(seed.Size() == 3 && seed[1].x == 3.0f, "authored points are taken as they are");
    }

    void TestInsertAndRemove()
    {
        Array<Vec2> points = { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };
        Check(JBro::PolygonEditModel::InsertOnEdge(points, 0, { 1, 0 }), "a point goes onto the first edge");
        Check(points.Size() == 5 && points[1].x == 1.0f && points[2].x == 2.0f, "between its two ends");
        Check(JBro::PolygonEditModel::InsertOnEdge(points, 4, { 0, 1 }), "and onto the closing edge");
        Check(points.Size() == 6 && points[5].y == 1.0f, "at the end of the list");
        Check(false == JBro::PolygonEditModel::InsertOnEdge(points, 6, { 9, 9 }), "an edge past the last is refused");

        Check(JBro::PolygonEditModel::RemoveVertex(points, 1), "a point comes off");
        Check(points.Size() == 5 && points[1].x == 2.0f, "and the rest close up");
        Array<Vec2> triangle = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
        Check(false == JBro::PolygonEditModel::RemoveVertex(triangle, 0), "a triangle keeps its three points");
        Check(triangle.Size() == 3, "untouched");
        Check(false == JBro::PolygonEditModel::RemoveVertex(points, 9), "a point past the last is refused");
    }
}

int RunPolygonEditModelTests()
{
    TestPickPrefersAVertexOverItsEdges();
    TestSeedStartsFromWhatIsDrawn();
    TestInsertAndRemove();
    std::cout << "Polygon edit model tests passed.\n";
    return 0;
}
