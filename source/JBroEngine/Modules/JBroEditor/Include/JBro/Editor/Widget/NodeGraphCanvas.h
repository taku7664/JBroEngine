#pragma once

#include <JBro/Editor/NodeGraph/NodeGraphView.h>
#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/Bool.h>

#include <imgui.h>

// 노드 그래프를 그리고 마우스를 받는 위젯이다(D-308, node-graph-plan §2.1).
//
//   ┌─────────────────────────────────────────────┐
//   │ ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  │   격자는 줌을 따른다
//   │   ┌─ Float ──┐        ┌─ Add ────────┐       │
//   │   │      ●───┼────────┼─● A   Result ●─┐   │   링크는 출력의 핀 타입 색이다
//   │   └──────────┘   ┌────┼─● B          │  │   │
//   │                  │    └──────────────┘  │   │
//   └─────────────────────────────────────────────┘
//
// 왼쪽 단추: 노드를 누르면 고르고 끌면 옮긴다(놓을 때 사건 하나), 핀에서 끌면 잇는다, 빈 곳에서 끌면 상자로 고른다.
// Ctrl·Shift 는 고름에 더하고 뺀다. Alt 를 누르고 링크나 입력 핀을 누르면 끊는다.
// 오른쪽·가운데 단추로 끌면 화면을 옮기고, 끌지 않은 오른쪽 단추는 우클릭 메뉴 사건이다. 휠은 마우스 아래를 고정한 채 줌한다.
//
// 위젯은 그래프를 고치지 않는다 - 손짓은 `events` 로 나가고, 패널이 커맨드로 바꾼다.
//
// **입력과 그리기가 둘이다.** `Begin` 이 입력을 받아 사건을 내고, 패널이 그 사건을 커맨드로 적용한 뒤 `End` 가 고친 그래프를 그린다.
// 하나로 두었을 때는 노드를 놓는 프레임에 미리 보기는 끝났는데 커맨드는 그리기 뒤에 들어가, 그 한 프레임 노드가 옛 자리로
// 순간이동했다 돌아왔다(사용자 지적). 잇기도 한 프레임 늦게 섰다.
namespace JBro::Widget
{
    struct NodeGraphCanvasState
    {
        NodeGraphView::Style style;
        NodeGraphView::Camera camera;
        NodeGraphView::Interaction interaction;
        // 처음 그릴 때 노드가 다 보이게 맞춘다. 사람이 카메라를 움직이기 전에 캔버스 크기가 바뀌면(도크가 갈라져 노드 인스펙터가 선다)
        // 다시 맞춘다 - 안 그러면 처음 연 그래프가 한쪽으로 치우쳐 보인다.
        Bool framed = false;
        ImVec2 framedSize;
        Vector2 framedPan;
        Float framedZoom = 1.0f;
        // 마우스 아래의 것이다. `End` 가 고친 그래프로 다시 잰다.
        NodeGraphView::Hit hovered;
        // `Begin` 이 적고 `End` 가 읽는다.
        ImVec2 canvasMin;
        ImVec2 canvasMax;
        Vector2 mouse;
        Bool canvasHovered = false;
    };

    // `size` 만큼의 칸을 잡고 입력을 사건으로 바꾼다. 그리지 않는다.
    void BeginNodeGraphCanvas(const char* id, const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state,
        const ImVec2& size, NodeGraphView::Events& events);
    // 그래프를 그린다. 사건을 적용한 뒤에 부른다. 시험이 노드를 찾을 수 있게 노드마다 `##node<번호>` 의 사각형을 알린다(`ItemLocator`).
    void EndNodeGraphCanvas(const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state);
}
