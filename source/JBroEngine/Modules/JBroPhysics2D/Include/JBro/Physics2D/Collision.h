#pragma once

#include <JBro/Physics2D/Geometry.h>
#include <JBro/Types/Math2D.h>

#include <cmath>
#include <cstdint>

// 2D 물리 커널의 좁은 판정이다(D-199, physics-plan §3.2·§4 의 2 단계).
//
// **법선은 언제나 A 에서 B 로 향하고, 기준면의 바깥 방향에서만 나온다.** 도형의 중심은 쓰지 않는다.
// 기존 엔진은 두 도형 중심을 잇는 방향으로 법선을 뒤집었는데, 오목 도형의 중심은 도형 밖에 있을 수 있어
// 홈 안쪽 벽에서 법선이 반대로 나왔다(physics-plan §1.2 의 1). 여기의 도형은 모두 볼록 조각이므로
// 기준면의 바깥 방향이 곧 답이다.
namespace JBro::Physics2D
{
    // 이보다 가까운(떨어져 있어도) 접촉은 미리 만든다. 솔버가 틈만큼은 다가오게 두고 그 이상은 막는다 -
    // 한 스텝에 틈을 뛰어넘어 박히는 것과, 닿았다 떨어졌다 하며 워밍스타트가 끊기는 것을 함께 줄인다.
    inline constexpr float SpeculativeDistance = 4.0f * LinearSlop;

    struct Rotation
    {
        float c = 1.0f;
        float s = 0.0f;

        static Rotation FromAngle(float angle)
        {
            return { std::cos(angle), std::sin(angle) };
        }
    };

    // 크기(scale)는 들지 않는다. 트랜스폼의 크기는 도형의 로컬 점에 미리 곱해 둔다.
    struct Pose
    {
        Vector2     position;
        Rotation rotation;
    };

    Vector2 TransformPoint(const Pose& pose, Vector2 local);
    Vector2 RotateVector(Rotation rotation, Vector2 local);

    struct Circle
    {
        Vector2  center;
        float radius = 0.0f;
    };

    struct ManifoldPoint
    {
        // 두 표면의 가운데 점(월드).
        Vector2          point;
        // 음수면 박힌 깊이, 양수면 아직 남은 틈이다.
        float         separation = 0.0f;
        // 같은 두 도형의 다음 스텝 접촉과 짝을 짓는 번호. 누적 임펄스를 이어 준다.
        std::uint32_t id = 0;
    };

    struct Manifold
    {
        Vector2          normal;
        ManifoldPoint points[2];
        std::uint32_t count = 0;
    };

    Manifold CollideCircles(const Circle& a, const Pose& poseA, const Circle& b, const Pose& poseB);
    Manifold CollidePolygonAndCircle(
        const ConvexPolygon& a, const Pose& poseA, const Circle& b, const Pose& poseB);
    Manifold CollidePolygons(
        const ConvexPolygon& a, const Pose& poseA, const ConvexPolygon& b, const Pose& poseB);

    // 체인의 선분 하나다(D-229). 두 면 모두에서 부딪히고, 이웃 선분의 먼 끝(유령 꼭짓점)을 알아 이음매에서 옆으로 걸리지 않는다:
    // 모서리 영역의 법선은 평평하거나 오목한 꼭짓점이면 선분의 면 법선으로 바로잡고, 볼록한 꼭짓점이면 두 면 사이만 받되 그
    // 꼭짓점을 끝(p2)으로 가진 선분만 맡는다. has* 가 거짓이면 체인의 끝이라 모서리를 그대로 받는다.
    struct ChainSegment
    {
        Vector2 p1;
        Vector2 p2;
        Vector2 previous;
        Vector2 next;
        bool hasPrevious = false;
        bool hasNext = false;
    };

    // 체인 선분이 A 다. 법선은 선분에서 상대 쪽이다.
    Manifold CollideChainSegmentAndPolygon(
        const ChainSegment& segment, const Pose& poseA, const ConvexPolygon& polygon, const Pose& poseB);
    Manifold CollideChainSegmentAndCircle(
        const ChainSegment& segment, const Pose& poseA, const Circle& circle, const Pose& poseB);

    Rect ComputePolygonBounds(const ConvexPolygon& polygon, const Pose& pose);
    Rect ComputeCircleBounds(const Circle& circle, const Pose& pose);

    // 반직선 질의. direction 은 단위 벡터다. 맞으면 origin 에서 표면까지의 거리와 그 자리의 바깥 법선을 준다.
    // 출발점이 이미 도형 안이면 거리 0, 법선은 -direction 으로 알린다 - 박힌 상태를 감추지 않는다(기존 엔진의 스윕과 같다).
    bool RaycastPolygon(const ConvexPolygon& polygon, const Pose& pose,
        Vector2 origin, Vector2 direction, float maxDistance, float& distance, Vector2& normal);
    bool RaycastCircle(const Circle& circle, const Pose& pose,
        Vector2 origin, Vector2 direction, float maxDistance, float& distance, Vector2& normal);

    // 겹침 질의. 맞닿기만 해도 겹친 것이다.
    bool OverlapPolygons(const ConvexPolygon& a, const Pose& poseA, const ConvexPolygon& b, const Pose& poseB);
    bool OverlapPolygonAndCircle(const ConvexPolygon& a, const Pose& poseA, const Circle& b, const Pose& poseB);
    // ── 점·겹침·스윕 질의 (physics-plan §4 의 6, 기존 엔진 `Physics2DQueryGeometry`) ─────────────────────────
    //
    // 스윕은 모양을 `direction`(단위 벡터)으로 `maxDistance` 까지 밀면서 **처음 닿는** 자리를 찾는다. 도형은 돌지 않는다.
    // 닿으면 그때까지 간 거리와 상대 표면의 바깥 법선(민 쪽을 향한다)을 준다. **출발부터 겹쳐 있으면 거리 0, 법선
    // -direction** 이다 - 파고든 상태를 감추지 않는다(기존 엔진과 같다).
    bool ContainsPoint(const ConvexPolygon& polygon, const Pose& pose, Vector2 point);
    bool ContainsPoint(const Circle& circle, const Pose& pose, Vector2 point);
    bool OverlapCircles(const Circle& a, const Pose& poseA, const Circle& b, const Pose& poseB);

    // 월드의 원(중심·반지름)을 민다.
    bool CastCircle(Vector2 center, float radius, Vector2 direction, float maxDistance,
        const ConvexPolygon& target, const Pose& targetPose, float& distance, Vector2& normal);
    bool CastCircle(Vector2 center, float radius, Vector2 direction, float maxDistance,
        const Circle& target, const Pose& targetPose, float& distance, Vector2& normal);
    // 볼록 조각을 `start` 자세에서 민다. 조각끼리는 민코프스키 차의 볼록 껍질에 반직선을 쏜다.
    bool CastPolygon(const ConvexPolygon& moving, const Pose& start, Vector2 direction, float maxDistance,
        const ConvexPolygon& target, const Pose& targetPose, float& distance, Vector2& normal);
    bool CastPolygon(const ConvexPolygon& moving, const Pose& start, Vector2 direction, float maxDistance,
        const Circle& target, const Pose& targetPose, float& distance, Vector2& normal);

}
