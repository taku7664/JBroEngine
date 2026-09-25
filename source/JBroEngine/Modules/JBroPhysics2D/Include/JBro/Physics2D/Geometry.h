#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

#include <cstdint>

// 2D 물리 커널의 도형 기하다(D-199, physics-plan §3.2).
//
// 여기 있는 함수는 **꼭짓점이 바뀔 때만** 부른다. 매 스텝 도는 경로가 아니므로 출력 배열이 자라는 것은
// 허용하지만, 호출자가 배열을 다시 쓰면 용량이 남아 두 번째부터는 할당하지 않는다.
namespace JBro::Physics2D
{
    // 볼록 조각 하나의 꼭짓점 상한. Box2D 와 같은 값이다. 조각이 고정 크기라 자식 도형이 POD 로 남는다.
    inline constexpr std::uint32_t MaxPolygonVertices = 8;

    // 이보다 가까운 두 점은 같은 점으로, 이웃 두 점을 잇는 선에서 이보다 가까운 점은 일직선으로 본다(유닛).
    inline constexpr float LinearSlop = 0.005f;

    // 반시계로 감긴 엄격한 볼록 다각형이다. 일직선 꼭짓점이 없다.
    struct ConvexPolygon
    {
        Vec2          points[MaxPolygonVertices];
        std::uint32_t count = 0;
    };

    enum class PolygonError : std::uint8_t
    {
        None,
        TooFewPoints,        // 정리한 뒤 세 점이 남지 않는다
        ZeroArea,            // 넓이가 없다
        SelfIntersecting,    // 변끼리 교차하거나 닿는다(구멍·8 자 모양 포함)
        DecompositionFailed, // 수치 문제로 귀를 찾지 못했다 - 넓이 일부를 버리지 않고 실패로 알린다
    };

    // 질량 속성. inertia 는 center 를 지나는 축 기준이다.
    struct MassData
    {
        float mass = 0.0f;
        Vec2  center;
        float inertia = 0.0f;
    };

    // 부호 있는 넓이. 반시계면 양수다. 오목 도형에서도 맞다(부채꼴 삼각형의 부호를 버리지 않는다).
    float SignedArea(ArrayView<const Vec2> points);

    // 거의 같은 점을 합치고, 일직선 점을 빼고, 반시계로 돌린 뒤 단순 다각형인지 검사한다.
    // 실패하면 out 을 비운다. 입력은 건드리지 않는다.
    PolygonError CleanPolygon(ArrayView<const Vec2> points, Array<Vec2>& out);

    // CleanPolygon 뒤에 귀 자르기 + Hertel-Mehlhorn 병합으로 볼록 조각을 만든다. 조각의 꼭짓점은
    // MaxPolygonVertices 이하다. 볼록한 입력이 상한 안이면 조각 하나다. 실패하면 outPieces 를 비운다.
    PolygonError DecomposePolygon(ArrayView<const Vec2> points, Array<ConvexPolygon>& outPieces);

    // 볼록 조각의 질량 속성(밀도 × 넓이).
    MassData ComputePolygonMass(const ConvexPolygon& polygon, float density);

    // 단순 다각형(오목 가능, 반시계) 외곽선에서 바로 구한 질량 속성. 조각 합과 독립인 두 번째 계산이라
    // 분해가 넓이나 관성을 잃지 않았는지 대조하는 데 쓴다.
    MassData ComputeOutlineMass(ArrayView<const Vec2> ccwPoints, float density);

    MassData ComputeCircleMass(Vec2 center, float radius, float density);

    // 여러 도형의 질량을 합친다. 관성은 합친 중심 기준으로 평행축 정리로 옮긴다.
    MassData CombineMass(ArrayView<const MassData> parts);
}
