#pragma once

#include <JBro/Editor/ViewCamera.h>
#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

#include <cstdint>

// 노드 그래프 화면의 모델이다(D-308, node-graph-plan §2.1). ImGui 를 모른다 - 노드·핀의 화면 자리, 마우스 아래의 것,
// 마우스 손짓을 사건으로 바꾸는 일을 여기서 하고 위젯(`Widget::NodeGraphCanvas`)은 그리기와 입력 옮기기만 한다.
// 그래서 시험이 화면 없이 판정과 손짓을 잰다(`PolygonEditModel` 과 같은 모양).
//
// 좌표는 둘이다. 그래프 공간은 노드의 `position` 이 사는 곳이고 y 가 아래로 커진다. 화면 공간은 픽셀이다.
// 화면 = 원점 + (그래프 - 팬) x 줌.
namespace JBro::NodeGraphView
{
    using NodeGraph::Domain;
    using NodeGraph::Graph;
    using NodeGraph::LinkId;
    using NodeGraph::Node;
    using NodeGraph::NodeId;
    using NodeGraph::NodeTypeInfo;
    using NodeGraph::PinRef;

    // 노드의 치수다. 그래프 공간의 값이고 줌이 곱해진다.
    struct Style
    {
        Float nodeWidth = 170.0f;
        Float headerHeight = 26.0f;
        Float rowHeight = 22.0f;
        Float bottomPadding = 8.0f;
        Float pinRadius = 5.0f;
        // 판정은 화면 픽셀이다 - 줌을 줄여도 핀을 잡을 수 있어야 한다.
        Float pinPickRadius = 9.0f;
        Float linkPickDistance = 6.0f;
        // 이만큼 움직여야 누름이 끌기가 된다(화면 픽셀).
        Float dragThreshold = 4.0f;
        Float gridStep = 32.0f;
        Float minZoom = 0.25f;
        Float maxZoom = 2.5f;
    };

    // 카메라는 에디터 공용의 부드러운 카메라다(`ViewCamera`, 캔버스 뷰와 같은 손맛). 그래프 공간이 그 카메라의 내용 공간이다.
    using Camera = ViewCamera::Camera;
    // `ToScreen`·`Follow` 은 `ViewCamera` 의 것을 그대로 쓴다(카메라가 그 이름공간의 타입이라 이름만으로 찾아진다).
    using ViewCamera::Follow;
    using ViewCamera::ToScreen;

    inline Vector2 ToGraph(const Camera& camera, Vector2 screen)
    {
        return ViewCamera::ToContent(camera, screen);
    }
    inline void SetCamera(Camera& camera, Vector2 pan, Float zoom)
    {
        ViewCamera::Set(camera, pan, zoom);
    }
    // 화면의 그 점 아래 그래프 자리를 그대로 둔 채 가려는 줌을 바꾼다. 기준점은 **가려는 카메라**로 잰다 - 따라가는 도중에
    // 휠을 거듭 돌려도 같은 점을 붙잡는다(캔버스 뷰와 같다).
    void ZoomAt(Camera& camera, const Style& style, Vector2 screen, Float zoom);
    // 노드들(`nodes` 가 비면 모두)이 다 보이게 가려는 카메라를 맞춘다. `immediate` 면 두 카메라를 함께 놓는다. 노드가 없으면 원점이다.
    void Frame(Camera& camera, const Style& style, const Graph& graph, const Domain& domain, Vector2 canvasSize,
        ArrayView<const NodeId> nodes = {}, Bool immediate = false);

    // 그래프 공간의 노드 사각형이다. 모르는 종류의 노드는 핀 없이 머리만 있다.
    Rect NodeBounds(const Node& node, const NodeTypeInfo* type, const Style& style);
    Vector2 InputPosition(const Node& node, std::size_t pin, const Style& style);
    Vector2 OutputPosition(const Node& node, std::size_t pin, const Style& style);

    // 링크 곡선(3 차 베지어)의 조절점이다. 출력에서 오른쪽으로 나가 입력으로 왼쪽에서 들어간다(화면 공간).
    struct Curve
    {
        Vector2 points[4];
    };
    Curve LinkCurve(Vector2 fromScreen, Vector2 toScreen, Float zoom);
    Float DistanceToCurve(const Curve& curve, Vector2 point);

    enum class HitKind : std::uint8_t
    {
        None,
        Node,
        Input,
        Output,
        Link,
    };

    struct Hit
    {
        HitKind kind = HitKind::None;
        NodeId node = NodeGraph::InvalidId;
        std::size_t pin = 0;
        LinkId link = NodeGraph::InvalidId;
    };

    // 화면 자리의 것을 찾는다. 핀 → 노드(뒤에 그린 것이 위) → 링크 차례다. `moveOffset` 은 끌고 있는 노드가 미리 옮겨 간 만큼이다.
    Hit Pick(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, Vector2 screen,
        ArrayView<const NodeId> moving = {}, Vector2 moveOffset = {});
    // 그래프 공간 사각형과 겹치는 노드다.
    void NodesInRect(const Graph& graph, const Domain& domain, const Style& style, const Rect& rect, Array<NodeId>& nodes);

    // 위젯이 한 프레임의 입력을 넘기는 꾸러미다. 위치는 화면 픽셀이다.
    struct Input
    {
        Vector2 mouse;
        // 캔버스 위에 있고 다른 창에 가리지 않았다.
        Bool hovered = false;
        Bool leftPressed = false;
        Bool leftDown = false;
        Bool leftReleased = false;
        Bool rightPressed = false;
        Bool rightReleased = false;
        Bool rightDown = false;
        Bool middleDown = false;
        // 왼쪽 단추를 두 번 눌렀다. 두 번째 누름은 `leftPressed` 도 참이다.
        Bool leftDoubleClicked = false;
        Vector2 mouseDelta;
        Float wheel = 0.0f;
        Bool ctrl = false;
        Bool shift = false;
        Bool alt = false;
    };

    // 손짓이 끝나 패널이 커맨드로 바꿀 것이다. 한 프레임에 여럿이 설 수 있다.
    struct Events
    {
        Bool selectionChanged = false;
        // 고른 노드들을 그만큼 옮겼다(그래프 공간). 끄는 동안이 아니라 놓을 때 한 번 선다.
        Bool moved = false;
        Vector2 moveDelta;
        Bool connect = false;
        PinRef connectFrom;
        PinRef connectTo;
        Bool disconnect = false;
        LinkId disconnectLink = NodeGraph::InvalidId;
        // 우클릭(끌지 않은 것). 그 자리의 것과 그래프 자리를 준다.
        Bool contextMenu = false;
        Hit contextHit;
        Vector2 contextPoint;
        // 핀에서 끌어 빈 곳에 놓았다. 노드 더하기 메뉴를 그 자리에 열고, 더한 노드에 이 핀을 잇는다.
        Bool linkDropped = false;
        PinRef droppedPin;
        Bool droppedFromOutput = false;
        Vector2 dropPoint;
        // 노드를 두 번 눌렀다. 패널이 노드 인스펙터를 앞으로 꺼낸다.
        Bool nodeDoubleClicked = false;
        NodeId doubleClickedNode = NodeGraph::InvalidId;
    };

    enum class Gesture : std::uint8_t
    {
        Idle,
        Pressing,
        MovingNodes,
        Linking,
        BoxSelecting,
        Panning,
    };

    // 고름과 끄는 중의 상태다. 패널마다 하나이고 커맨드에 들지 않는다(고름은 되돌리지 않는다).
    class Interaction final
    {
    public:
        void Update(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input, Events& events);

        ArrayView<const NodeId> GetSelection() const noexcept { return m_selection.View(); }
        Bool IsSelected(NodeId id) const noexcept;
        void Select(ArrayView<const NodeId> nodes);
        void ClearSelection();
        // 그래프에서 사라진 노드를 고름에서 뺀다. 되돌리기 뒤에 부른다.
        void Prune(const Graph& graph);

        Gesture GetGesture() const noexcept { return m_gesture; }
        // 끄는 동안 고른 노드가 미리 옮겨 간 만큼이다(그래프 공간). 그릴 때 더한다.
        Vector2 GetMoveOffset() const noexcept { return m_gesture == Gesture::MovingNodes ? m_moveOffset : Vector2{}; }
        // 잇는 중이면 그 핀이다.
        Bool GetLinkOrigin(PinRef& pin, Bool& fromOutput) const noexcept;
        Vector2 GetPressPoint() const noexcept { return m_pressScreen; }
        // 상자로 고르는 중이면 그 화면 사각형이다.
        Bool GetBoxRect(Rect& rect) const noexcept;
        // 잇는 중에 마우스 아래 핀이 이을 수 있는 것인가(그릴 때 핀을 밝힌다).
        Bool CanLinkTo(const Graph& graph, const Domain& domain, const Hit& hit) const;

    private:
        void Release(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, const Input& input, Events& events);

        Array<NodeId> m_selection;
        Gesture m_gesture = Gesture::Idle;
        Hit m_pressHit;
        Vector2 m_pressScreen;
        Vector2 m_mouse;
        Vector2 m_moveOffset;
        Bool m_pressAdds = false;
        Bool m_rightDragged = false;
        Vector2 m_rightPress;
    };
}
