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
namespace JBro::Widget
{
    struct NodeGraphCanvasState
    {
        NodeGraphView::Style style;
        NodeGraphView::Camera camera;
        NodeGraphView::Interaction interaction;
        // 처음 그릴 때 노드가 다 보이게 맞춘다.
        Bool framed = false;
        // 마우스 아래의 것이다(지난 프레임). 그릴 때 밝히고 시험이 읽는다.
        NodeGraphView::Hit hovered;
    };

    // `size` 만큼의 칸을 쓴다.
    void NodeGraphCanvas(const char* id, const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state,
        const ImVec2& size, NodeGraphView::Events& events);
}
