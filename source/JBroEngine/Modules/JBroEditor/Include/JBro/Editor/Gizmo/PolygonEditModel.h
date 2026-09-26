#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

#include <cstdint>

// 캔버스 뷰의 폴리곤 콜라이더 편집(physics-plan §4 의 5 단계, 기존 엔진 `CCanvasViewTool` 의 버텍스 편집)에서
// 화면을 모르는 부분이다. `GizmoModel` 처럼 ImGui 없이 숫자로 잰다.
namespace JBro::PolygonEditModel
{
    // 버텍스 손잡이를 잡는 반지름과, 변 위에 버텍스를 넣는 거리(화면 픽셀).
    inline constexpr float VertexPickRadius = 7.0f;
    inline constexpr float EdgePickDistance = 6.0f;
    // 이보다 적으면 도형이 되지 않는다. 지우기가 여기서 멈춘다.
    inline constexpr std::uint32_t MinVertexCount = 3;

    enum class HitKind : std::uint8_t
    {
        None,
        Vertex,
        Edge,
    };

    struct Hit
    {
        HitKind       kind = HitKind::None;
        // Vertex 면 그 버텍스, Edge 면 변의 시작 버텍스(끝은 다음 것)다.
        std::uint32_t index = 0;
        // Edge 일 때 넣을 자리(화면)다. 마우스에서 변에 내린 수선의 발이다.
        Vec2          point;
    };

    // 화면에 그려진 버텍스(`screen`, 고리)에서 마우스가 가리키는 것. **버텍스가 변보다 먼저다** - 버텍스 바로 옆은
    // 두 변에도 가까워, 변을 먼저 보면 버텍스를 잡으려다 새 버텍스를 만든다.
    // closed 가 거짓이면(열린 체인) 마지막에서 처음으로 가는 변은 없다.
    Hit Pick(ArrayView<const Vec2> screen, Vec2 mouse, bool closed = true);

    // 편집이 시작할 꼭짓점. `points` 가 비었으면 `size` 상자의 네 모서리다(offset 은 빼고) - 물리와 캔버스 뷰가
    // 빈 폴리곤을 그 상자로 다루므로(physics-plan §4 의 5), 첫 편집이 보이는 모양에서 이어진다. 빈 체인은 `size.x` 폭의
    // 가로 선분 두 점이다(D-228, 물리와 같다).
    void SeedPoints(const Component::Collider2D& collider, Array<Vec2>& out);

    // `edge` 번 변(그 버텍스와 다음 버텍스 사이)에 `point` 를 넣는다. 변 번호가 없으면 거짓이다.
    bool InsertOnEdge(Array<Vec2>& points, std::uint32_t edge, Vec2 point);
    // `index` 번 버텍스를 뺀다. 남는 것이 `MinVertexCount` 보다 적어지면 거짓이고 바꾸지 않는다.
    bool RemoveVertex(Array<Vec2>& points, std::uint32_t index, std::uint32_t minimum = MinVertexCount);

    // 포인트로 모양을 정하는 콜라이더인가(Polygon·Chain, D-228).
    bool EditsPoints(const Component::Collider2D& collider);
    // 외곽선이 닫혀 있는가. 열린 체인만 거짓이다.
    bool IsClosedOutline(const Component::Collider2D& collider);
    // 지우기가 멈추는 포인트 수: 폴리곤과 닫힌 체인은 셋, 열린 체인은 둘이다.
    std::uint32_t MinPointCount(const Component::Collider2D& collider);
}
