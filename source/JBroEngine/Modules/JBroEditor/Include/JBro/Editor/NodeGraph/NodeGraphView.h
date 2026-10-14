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
//
// 그래프에 놓이는 것은 노드·링크(경유점)·주석 셋이다(D-321). 고름은 번호 하나로 노드와 주석을 함께 든다 - 둘은 한 번호 줄을 쓴다.
namespace JBro::NodeGraphView
{
    using NodeGraph::Comment;
    using NodeGraph::CommentId;
    using NodeGraph::Domain;
    using NodeGraph::Graph;
    using NodeGraph::LinkId;
    using NodeGraph::Node;
    using NodeGraph::NodeId;
    using NodeGraph::NodeTypeInfo;
    using NodeGraph::PinRef;

    // 노드의 치수다. 그래프 공간의 값이고 줌이 곱해진다. "화면 픽셀" 이라 적은 것은 줌과 상관없다.
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

        // ── 노드 안 값 칸(D-321). 이어지지 않은 입력의 값을 핀 왼쪽에서 바로 고친다(Unity 셰이더 그래프와 같은 자리). ──
        // 칸 하나의 너비, 핀과의 틈, 칸이 서는 가장 작은 줌. 그보다 작으면 글자가 읽히지 않아 칸을 세우지 않는다.
        Float fieldWidth = 38.0f;
        Float fieldGap = 10.0f;
        Float fieldMinZoom = 0.45f;

        // ── 경유점(D-321). 그리는 반지름과 잡는 반지름(화면 픽셀). ──
        Float waypointRadius = 4.0f;
        Float waypointPickRadius = 8.0f;

        // ── 주석(D-321). 머리 높이(그래프 공간), 크기 손잡이(화면 픽셀), 가장 작은 크기, 고른 노드를 감쌀 때의 여백. ──
        Float commentHeaderHeight = 28.0f;
        Float commentHandle = 12.0f;
        Float commentMinWidth = 120.0f;
        Float commentMinHeight = 70.0f;
        Float commentPadding = 24.0f;

        // ── 미니맵(D-321). 오른쪽 아래에 선다(화면 픽셀). ──
        Bool showMinimap = true;
        Float minimapWidth = 190.0f;
        Float minimapHeight = 130.0f;
        Float minimapMargin = 10.0f;
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
    // 노드들(`items` 가 비면 모두, 주석 번호도 받는다)이 다 보이게 가려는 카메라를 맞춘다. `immediate` 면 두 카메라를 함께 놓는다.
    // 아무것도 없으면 원점이다.
    void Frame(Camera& camera, const Style& style, const Graph& graph, const Domain& domain, Vector2 canvasSize,
        ArrayView<const UInt64> items = {}, Bool immediate = false);
    // 그래프 공간의 점이 화면 가운데 오게 가려는 카메라를 옮긴다(미니맵이 쓴다). 줌은 그대로다.
    void CenterOn(Camera& camera, Vector2 canvasSize, Vector2 graphPoint);

    // 그래프 공간의 노드 사각형이다. 모르는 종류의 노드는 핀 없이 머리만 있다.
    Rect NodeBounds(const Node& node, const NodeTypeInfo* type, const Style& style);
    Vector2 InputPosition(const Node& node, std::size_t pin, const Style& style);
    Vector2 OutputPosition(const Node& node, std::size_t pin, const Style& style);
    Rect CommentBounds(const Comment& comment);
    // 노드·주석을 다 덮는 그래프 공간 사각형이다. 아무것도 없으면 거짓이다.
    Bool ContentBounds(const Graph& graph, const Domain& domain, const Style& style, Rect& bounds);

    // ── 노드 안 값 칸(D-321) ──
    // 그 입력의 값 칸 수다. 이어지지 않았고, 핀 타입이 한 칸에서 네 칸이고, 줌이 `fieldMinZoom` 이상일 때만 0 이 아니다.
    UInt32 InputFieldComponents(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, const Node& node, std::size_t pin);
    // 값 칸의 그래프 공간 사각형이다(핀 왼쪽). 칸 수는 핀 타입의 칸 수다.
    Rect InputFieldBounds(const Node& node, std::size_t pin, UInt32 components, const Style& style);

    // 링크 곡선(3 차 베지어)의 조절점이다. 출력에서 오른쪽으로 나가 입력으로 왼쪽에서 들어간다(화면 공간).
    struct Curve
    {
        Vector2 points[4];
    };
    Curve LinkCurve(Vector2 fromScreen, Vector2 toScreen, Float zoom);
    Float DistanceToCurve(const Curve& curve, Vector2 point);
    // 경유점을 지나는 링크의 곡선들이다(화면 공간). 점마다 가로로 들어가 가로로 나간다 - 곡선 수는 경유점 수 + 1 이다.
    void LinkPath(Vector2 fromScreen, ArrayView<const Vector2> waypointsScreen, Vector2 toScreen, Float zoom, Array<Curve>& curves);

    // ── 미니맵(D-321) ──
    struct Minimap
    {
        // 미니맵 칸의 화면 사각형이다.
        Rect screen;
        // 미니맵이 보이는 그래프 공간이다(내용과 지금 보이는 곳을 함께 덮는다).
        Rect content;
        // 그래프 한 단위가 미니맵의 몇 픽셀인가.
        Float scale = 1.0f;
        // 내용을 가운데 두는 미니맵 안의 틈이다(화면 픽셀).
        Vector2 offset;
    };
    // 미니맵의 자리다. 꺼져 있거나, 그릴 것이 없거나, 캔버스가 너무 작으면 거짓이다.
    Bool LayoutMinimap(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, Vector2 canvasSize, Minimap& minimap);
    Vector2 MinimapToGraph(const Minimap& minimap, Vector2 screen);
    Vector2 GraphToMinimap(const Minimap& minimap, Vector2 graphPoint);

    enum class HitKind : std::uint8_t
    {
        None,
        Node,
        Input,
        Output,
        Link,
        // 노드 안 값 칸이다. 손짓을 시작하지 않는다 - 그 칸이 마우스를 받는다.
        Field,
        // 링크의 경유점이다. `link` 와 `waypoint` 가 찬다.
        Waypoint,
        // 주석의 머리(고르고 끄는 곳)와 오른쪽 아래 크기 손잡이다. `comment` 가 찬다.
        CommentHeader,
        CommentResize,
        Minimap,
    };

    struct Hit
    {
        HitKind kind = HitKind::None;
        NodeId node = NodeGraph::InvalidId;
        std::size_t pin = 0;
        LinkId link = NodeGraph::InvalidId;
        std::size_t waypoint = 0;
        CommentId comment = NodeGraph::InvalidId;
    };

    // 끌고 있는 것이 미리 옮겨 간 것이다. 그리기와 판정이 같은 것을 본다.
    struct Preview
    {
        // 그만큼 옮겨 간 노드·주석 번호들이다.
        ArrayView<const UInt64> moving;
        Vector2 moveOffset;
        // 끌고 있는 경유점이다.
        LinkId waypointLink = NodeGraph::InvalidId;
        std::size_t waypointIndex = 0;
        Vector2 waypointPoint;
        // 크기를 바꾸는 주석이다.
        CommentId resizing = NodeGraph::InvalidId;
        Vector2 resizedSize;
    };

    // 미리 옮긴 것을 더한 자리다.
    Node PlacedNode(const Node& node, const Preview& preview);
    Comment PlacedComment(const Comment& comment, const Preview& preview);
    // 경유점들의 그래프 자리다. 두 끝 노드가 함께 옮겨 가면 경유점도 따라간다.
    void PlacedWaypoints(const NodeGraph::Link& link, const Preview& preview, Array<Vector2>& points);

    // 화면 자리의 것을 찾는다. 미니맵 → 핀 → 값 칸 → 노드(뒤에 그린 것이 위) → 경유점 → 링크 → 주석(뒤에 그린 것이 위) 차례다.
    // 그리는 차례의 거꾸로다 - 위에 보이는 것이 먼저 잡힌다. `canvasSize` 가 없으면 미니맵은 보지 않는다.
    Hit Pick(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, Vector2 screen,
        const Preview& preview = {}, Vector2 canvasSize = {});
    // 그래프 공간 사각형과 겹치는 노드와, 그 안에 다 들어가는 주석이다.
    void ItemsInRect(const Graph& graph, const Domain& domain, const Style& style, const Rect& rect, Array<UInt64>& items);
    // 주석 안에 다 들어가는 노드와 주석이다(주석을 끌면 함께 간다).
    void ItemsInComment(const Graph& graph, const Domain& domain, const Style& style, const Comment& comment, Array<UInt64>& items);
    // 링크 위의 화면 점에 경유점을 끼울 자리(그 점이 가장 가까운 곡선의 차례)다.
    std::size_t WaypointInsertIndex(const Graph& graph, const Style& style, const Camera& camera, const NodeGraph::Link& link, Vector2 screen);

    // 위젯이 한 프레임의 입력을 넘기는 꾸러미다. 위치는 화면 픽셀이다.
    struct Input
    {
        Vector2 mouse;
        // 캔버스의 크기다(화면 픽셀). 미니맵이 이것으로 자리를 잡는다.
        Vector2 canvasSize;
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
        // 노드·주석들을 그만큼 옮겼다(그래프 공간). 끄는 동안이 아니라 놓을 때 한 번 선다. 주석을 끌면 그 안의 노드도 든다.
        Bool moved = false;
        Vector2 moveDelta;
        Array<UInt64> movedItems;
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
        // 노드나 주석 머리를 두 번 눌렀다. 패널이 노드 인스펙터를 앞으로 꺼낸다.
        Bool nodeDoubleClicked = false;
        NodeId doubleClickedNode = NodeGraph::InvalidId;
        Bool commentDoubleClicked = false;
        CommentId doubleClickedComment = NodeGraph::InvalidId;
        // ── 경유점(D-321) ── 링크를 두 번 누르면 더하고, 끌어 놓으면 옮기고, Alt 로 누르면 지운다.
        Bool waypointAdded = false;
        Bool waypointMoved = false;
        Bool waypointRemoved = false;
        LinkId waypointLink = NodeGraph::InvalidId;
        std::size_t waypointIndex = 0;
        Vector2 waypointPoint;
        // ── 주석의 크기(D-321) ── 손잡이를 놓을 때 한 번 선다.
        Bool commentResized = false;
        CommentId resizedComment = NodeGraph::InvalidId;
        Vector2 resizedSize;
        // ── 노드 안 값 칸(D-321) ── 위젯의 `End` 가 채운다. 끄는 동안 프레임마다 선다(패널이 커맨드 하나로 합친다).
        Bool inputEdited = false;
        NodeId editedNode = NodeGraph::InvalidId;
        std::size_t editedPin = 0;
        NodeGraph::PinValue editedValue;
    };

    enum class Gesture : std::uint8_t
    {
        Idle,
        Pressing,
        MovingNodes,
        Linking,
        BoxSelecting,
        Panning,
        MovingWaypoint,
        ResizingComment,
        Minimap,
    };

    // 고름과 끄는 중의 상태다. 패널마다 하나이고 커맨드에 들지 않는다(고름은 되돌리지 않는다).
    class Interaction final
    {
    public:
        void Update(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input, Events& events);

        // 고른 노드와 주석의 번호다.
        ArrayView<const UInt64> GetSelection() const noexcept { return m_selection.View(); }
        Bool IsSelected(UInt64 id) const noexcept;
        void Select(ArrayView<const UInt64> items);
        void ClearSelection();
        // 그래프에서 사라진 노드·주석을 고름에서 뺀다. 되돌리기 뒤에 부른다.
        void Prune(const Graph& graph);

        Gesture GetGesture() const noexcept { return m_gesture; }
        // 끄는 동안 미리 옮겨 간 것이다. 그릴 때와 판정할 때 더한다.
        Preview GetPreview() const noexcept;
        // 끄는 동안 함께 가는 것이 미리 옮겨 간 만큼이다(그래프 공간).
        Vector2 GetMoveOffset() const noexcept { return m_gesture == Gesture::MovingNodes ? m_moveOffset : Vector2{}; }
        Bool IsMoving(UInt64 id) const noexcept;
        // 잇는 중이면 그 핀이다.
        Bool GetLinkOrigin(PinRef& pin, Bool& fromOutput) const noexcept;
        Vector2 GetPressPoint() const noexcept { return m_pressScreen; }
        // 상자로 고르는 중이면 그 화면 사각형이다.
        Bool GetBoxRect(Rect& rect) const noexcept;
        // 잇는 중에 마우스 아래 핀이 이을 수 있는 것인가(그릴 때 핀을 밝힌다).
        Bool CanLinkTo(const Graph& graph, const Domain& domain, const Hit& hit) const;

    private:
        void Press(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input, Events& events);
        void Release(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, const Input& input, Events& events);
        void BeginMove(const Graph& graph, const Domain& domain, const Style& style);
        void FollowMinimap(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input);

        Array<UInt64> m_selection;
        // 끄는 동안 함께 가는 것이다(고른 것과, 고른 주석 안에 든 것).
        Array<UInt64> m_moving;
        Gesture m_gesture = Gesture::Idle;
        Hit m_pressHit;
        Vector2 m_pressScreen;
        Vector2 m_mouse;
        Vector2 m_moveOffset;
        // 끄는 경유점의 처음 자리와 지금 자리, 크기를 바꾸는 주석의 처음 크기와 지금 크기다.
        Vector2 m_waypointStart;
        Vector2 m_waypointPoint;
        Vector2 m_resizeStart;
        Vector2 m_resizedSize;
        Bool m_pressAdds = false;
        Bool m_rightDragged = false;
        Vector2 m_rightPress;
    };
}
