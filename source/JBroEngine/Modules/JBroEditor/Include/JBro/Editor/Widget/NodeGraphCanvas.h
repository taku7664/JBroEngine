#pragma once

#include <JBro/Editor/NodeGraph/NodeGraphView.h>
#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/String.h>

#include <imgui.h>

// 노드 그래프를 그리고 마우스를 받는 위젯이다(D-308, node-graph-plan §2.1).
//
//   ┌─────────────────────────────────────────────┐
//   │ ┌ Inputs ────────────────┐ ·  ·  ·  ·  ·  ·  │   주석은 노드 뒤에 깔린다(D-313)
//   │ │ [2.00]●┌─ Add ───────┐ │      ┌─ Output ┐ │
//   │ │ [1.00]●│ A   Result ●┼─┼──◆───┼─● Value  │ │   ◆ 는 링크의 경유점이다
//   │ │        └─────────────┘ │      └─────────┘ │   [2.00] 은 이어지지 않은 입력의 값 칸이다
//   │ └────────────────────────┘           ┌────┐ │
//   │                                      │ ▫▫ │ │   오른쪽 아래는 미니맵이다
//   └──────────────────────────────────────┴────┴─┘
//
// 왼쪽 단추: 노드를 누르면 고르고 끌면 옮긴다(놓을 때 사건 하나), 핀에서 끌면 잇는다, 빈 곳에서 끌면 상자로 고른다.
// 주석은 머리를 눌러 고르고 끌면 안에 든 노드와 함께 가고, 오른쪽 아래 모서리를 끌면 크기가 바뀐다.
// 링크를 두 번 누르면 경유점이 서고, 경유점은 끌어 옮긴다. Ctrl·Shift 는 고름에 더하고 뺀다. Alt 를 누르고 링크·입력 핀·경유점을
// 누르면 끊거나 지운다. 오른쪽·가운데 단추로 끌면 화면을 옮기고, 끌지 않은 오른쪽 단추는 우클릭 메뉴 사건이다. 휠은 마우스 아래를
// 고정한 채 줌한다. 미니맵을 누르거나 끌면 그 자리로 간다.
//
// 위젯은 그래프를 고치지 않는다 - 손짓은 `events` 로 나가고, 패널이 커맨드로 바꾼다.
//
// **입력과 그리기가 둘이다.** `Begin` 이 입력을 받아 사건을 내고, 패널이 그 사건을 커맨드로 적용한 뒤 `End` 가 고친 그래프를 그린다.
// 하나로 두었을 때는 노드를 놓는 프레임에 미리 보기는 끝났는데 커맨드는 그리기 뒤에 들어가, 그 한 프레임 노드가 옛 자리로
// 순간이동했다 돌아왔다(사용자 지적). 잇기도 한 프레임 늦게 섰다. 노드 안 값 칸은 그리면서 고쳐지므로 `End` 가 사건을 낸다.
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
    // 그래프를 그린다. 사건을 적용한 뒤에 부른다. 노드 안 값 칸을 고치면 `events` 의 `inputEdited` 가 선다 - 패널이 이어서 적용한다.
    // 시험이 찾을 수 있게 노드마다 `##node<번호>`, 값 칸마다 `##field<노드>_<핀>`, 주석 머리마다 `##comment<번호>`,
    // 경유점마다 `##waypoint<링크>_<차례>`, 미니맵은 `##minimap` 의 사각형을 알린다(`ItemLocator`).
    void EndNodeGraphCanvas(const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state,
        NodeGraphView::Events& events);

    // ── 노드 찾기 메뉴(D-313) ──
    // 우클릭 메뉴 안에 그린다. 맨 위가 찾기 칸이고 메뉴가 열릴 때 글자 칸에 포커스가 간다. 칸이 비면 묶음마다 하위 메뉴로,
    // 글자를 치면 이름이나 묶음에 그 글자가 든 것을 한 줄로 보인다. 위·아래 화살표로 고르고 Enter 로 더한다.
    struct NodeMenuItem
    {
        const char* name = nullptr;
        // 비면 맨 위에 둔다.
        const char* category = nullptr;
        Bool enabled = true;
    };
    // 고른 항목의 차례를 낸다. 고르지 않았으면 -1 이다. `filter`·`highlight` 는 부르는 쪽이 메뉴가 열린 동안 들고 있다.
    Int32 NodeSearchMenu(String& filter, Int32& highlight, ArrayView<const NodeMenuItem> items, const char* disabledTooltip);
}
