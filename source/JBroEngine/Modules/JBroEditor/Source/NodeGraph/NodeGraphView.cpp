#include <JBro/Editor/NodeGraph/NodeGraphView.h>

#include <algorithm>
#include <cmath>

namespace JBro::NodeGraphView
{
    namespace
    {
        float Length(Vector2 value)
        {
            return std::sqrt(value.x.Get() * value.x.Get() + value.y.Get() * value.y.Get());
        }

        Vector2 Sub(Vector2 a, Vector2 b)
        {
            return Vector2{a.x.Get() - b.x.Get(), a.y.Get() - b.y.Get()};
        }

        Vector2 Add(Vector2 a, Vector2 b)
        {
            return Vector2{a.x.Get() + b.x.Get(), a.y.Get() + b.y.Get()};
        }

        Vector2 Scale(Vector2 a, float s)
        {
            return Vector2{a.x.Get() * s, a.y.Get() * s};
        }

        Bool Contains(ArrayView<const NodeId> nodes, NodeId id)
        {
            for (const NodeId& node : nodes)
            {
                if (node.Get() == id.Get())
                {
                    return true;
                }
            }
            return false;
        }

        // 끌고 있는 노드는 미리 옮겨 간 자리로 본다 - 놓기 전에도 핀을 그 자리에서 잡는다.
        Node Shifted(const Node& node, ArrayView<const NodeId> moving, Vector2 offset)
        {
            Node shifted;
            shifted.id = node.id;
            shifted.position = Contains(moving, node.id) ? Add(node.position, offset) : node.position;
            return shifted;
        }

        Rect MakeRect(Vector2 a, Vector2 b)
        {
            Rect rect;
            rect.min = Vector2{std::min(a.x.Get(), b.x.Get()), std::min(a.y.Get(), b.y.Get())};
            rect.max = Vector2{std::max(a.x.Get(), b.x.Get()), std::max(a.y.Get(), b.y.Get())};
            return rect;
        }

        Bool Overlaps(const Rect& a, const Rect& b)
        {
            return a.min.x.Get() <= b.max.x.Get() && b.min.x.Get() <= a.max.x.Get()
                && a.min.y.Get() <= b.max.y.Get() && b.min.y.Get() <= a.max.y.Get();
        }
    }

    Vector2 ToScreen(const Camera& camera, Vector2 graph)
    {
        return Add(camera.origin, Scale(Sub(graph, camera.pan), camera.zoom.Get()));
    }

    Vector2 ToGraph(const Camera& camera, Vector2 screen)
    {
        const float zoom = camera.zoom.Get() <= 0.0f ? 1.0f : camera.zoom.Get();
        return Add(camera.pan, Scale(Sub(screen, camera.origin), 1.0f / zoom));
    }

    void ZoomAt(Camera& camera, const Style& style, Vector2 screen, Float zoom)
    {
        const Vector2 anchor = ToGraph(camera, screen);
        camera.zoom = std::clamp(zoom.Get(), style.minZoom.Get(), style.maxZoom.Get());
        // 줌을 바꾼 뒤에도 그 화면 자리 아래에 같은 그래프 자리가 오게 팬을 다시 잡는다.
        camera.pan = Sub(anchor, Scale(Sub(screen, camera.origin), 1.0f / camera.zoom.Get()));
    }

    void Frame(Camera& camera, const Style& style, const Graph& graph, const Domain& domain, Vector2 canvasSize)
    {
        if (graph.GetNodes().IsEmpty())
        {
            camera.zoom = 1.0f;
            camera.pan = Vector2{};
            return;
        }
        Rect bounds = NodeBounds(graph.GetNodes()[0], domain.FindNodeType(graph.GetNodes()[0].type.View()), style);
        for (const Node& node : graph.GetNodes())
        {
            const Rect rect = NodeBounds(node, domain.FindNodeType(node.type.View()), style);
            bounds.min = Vector2{std::min(bounds.min.x.Get(), rect.min.x.Get()), std::min(bounds.min.y.Get(), rect.min.y.Get())};
            bounds.max = Vector2{std::max(bounds.max.x.Get(), rect.max.x.Get()), std::max(bounds.max.y.Get(), rect.max.y.Get())};
        }
        const float margin = 40.0f;
        const float width = bounds.max.x.Get() - bounds.min.x.Get() + margin * 2.0f;
        const float height = bounds.max.y.Get() - bounds.min.y.Get() + margin * 2.0f;
        float zoom = 1.0f;
        if (canvasSize.x.Get() > 0.0f && canvasSize.y.Get() > 0.0f)
        {
            zoom = std::min(canvasSize.x.Get() / width, canvasSize.y.Get() / height);
        }
        camera.zoom = std::clamp(zoom, style.minZoom.Get(), std::min(1.0f, style.maxZoom.Get()));
        // 가운데를 맞춘다.
        const Vector2 center{(bounds.min.x.Get() + bounds.max.x.Get()) * 0.5f, (bounds.min.y.Get() + bounds.max.y.Get()) * 0.5f};
        camera.pan = Sub(center, Scale(canvasSize, 0.5f / camera.zoom.Get()));
    }

    Rect NodeBounds(const Node& node, const NodeTypeInfo* type, const Style& style)
    {
        const std::size_t rows = type == nullptr ? 0 : std::max(type->inputs.Size(), type->outputs.Size());
        Rect rect;
        rect.min = node.position;
        rect.max = Vector2{node.position.x.Get() + style.nodeWidth.Get(),
            node.position.y.Get() + style.headerHeight.Get() + static_cast<float>(rows) * style.rowHeight.Get() + style.bottomPadding.Get()};
        return rect;
    }

    Vector2 InputPosition(const Node& node, std::size_t pin, const Style& style)
    {
        return Vector2{node.position.x.Get(),
            node.position.y.Get() + style.headerHeight.Get() + (static_cast<float>(pin) + 0.5f) * style.rowHeight.Get()};
    }

    Vector2 OutputPosition(const Node& node, std::size_t pin, const Style& style)
    {
        return Vector2{node.position.x.Get() + style.nodeWidth.Get(),
            node.position.y.Get() + style.headerHeight.Get() + (static_cast<float>(pin) + 0.5f) * style.rowHeight.Get()};
    }

    Curve LinkCurve(Vector2 fromScreen, Vector2 toScreen, Float zoom)
    {
        // 가로로 나가고 들어오게 조절점을 둔다. 둘이 가까워도 고리처럼 휘어 보이게 최소 거리를 둔다.
        const float reach = std::max(std::fabs(toScreen.x.Get() - fromScreen.x.Get()) * 0.5f, 50.0f * zoom.Get());
        Curve curve;
        curve.points[0] = fromScreen;
        curve.points[1] = Vector2{fromScreen.x.Get() + reach, fromScreen.y.Get()};
        curve.points[2] = Vector2{toScreen.x.Get() - reach, toScreen.y.Get()};
        curve.points[3] = toScreen;
        return curve;
    }

    Float DistanceToCurve(const Curve& curve, Vector2 point)
    {
        // 곡선을 짧은 선분으로 나누어 가장 가까운 거리를 잰다. 판정에만 쓰므로 스물네 조각이면 픽셀 안이다.
        constexpr int Segments = 24;
        float best = 1.0e30f;
        Vector2 previous = curve.points[0];
        for (int index = 1; index <= Segments; ++index)
        {
            const float t = static_cast<float>(index) / Segments;
            const float u = 1.0f - t;
            const float w0 = u * u * u;
            const float w1 = 3.0f * u * u * t;
            const float w2 = 3.0f * u * t * t;
            const float w3 = t * t * t;
            const Vector2 current{
                curve.points[0].x.Get() * w0 + curve.points[1].x.Get() * w1 + curve.points[2].x.Get() * w2 + curve.points[3].x.Get() * w3,
                curve.points[0].y.Get() * w0 + curve.points[1].y.Get() * w1 + curve.points[2].y.Get() * w2 + curve.points[3].y.Get() * w3};
            const Vector2 segment = Sub(current, previous);
            const float lengthSquared = segment.x.Get() * segment.x.Get() + segment.y.Get() * segment.y.Get();
            float along = 0.0f;
            if (lengthSquared > 0.0f)
            {
                const Vector2 toPoint = Sub(point, previous);
                along = std::clamp((toPoint.x.Get() * segment.x.Get() + toPoint.y.Get() * segment.y.Get()) / lengthSquared, 0.0f, 1.0f);
            }
            best = std::min(best, Length(Sub(point, Add(previous, Scale(segment, along)))));
            previous = current;
        }
        return best;
    }

    Hit Pick(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, Vector2 screen,
        ArrayView<const NodeId> moving, Vector2 moveOffset)
    {
        const ArrayView<const Node> nodes = graph.GetNodes();
        const float pinRadius = style.pinPickRadius.Get();
        // 핀이 먼저다 - 노드 가장자리에 걸쳐 있어 노드 판정이 먼저면 핀을 잡을 수 없다. 뒤에 그린 노드가 위에 있다.
        for (std::size_t reverse = nodes.Size(); reverse > 0; --reverse)
        {
            const Node node = Shifted(nodes[reverse - 1], moving, moveOffset);
            const NodeTypeInfo* type = domain.FindNodeType(nodes[reverse - 1].type.View());
            if (type == nullptr)
            {
                continue;
            }
            for (std::size_t pin = 0; pin < type->outputs.Size(); ++pin)
            {
                if (Length(Sub(ToScreen(camera, OutputPosition(node, pin, style)), screen)) <= pinRadius)
                {
                    return Hit{HitKind::Output, node.id, pin, NodeGraph::InvalidId};
                }
            }
            for (std::size_t pin = 0; pin < type->inputs.Size(); ++pin)
            {
                if (Length(Sub(ToScreen(camera, InputPosition(node, pin, style)), screen)) <= pinRadius)
                {
                    return Hit{HitKind::Input, node.id, pin, NodeGraph::InvalidId};
                }
            }
        }
        const Vector2 graphPoint = ToGraph(camera, screen);
        for (std::size_t reverse = nodes.Size(); reverse > 0; --reverse)
        {
            const Node node = Shifted(nodes[reverse - 1], moving, moveOffset);
            if (NodeBounds(node, domain.FindNodeType(nodes[reverse - 1].type.View()), style).Contains(graphPoint))
            {
                return Hit{HitKind::Node, node.id, 0, NodeGraph::InvalidId};
            }
        }
        Float bestDistance = style.linkPickDistance;
        Hit best;
        for (const NodeGraph::Link& link : graph.GetLinks())
        {
            const Node* from = graph.FindNode(link.from.node);
            const Node* to = graph.FindNode(link.to.node);
            if (from == nullptr || to == nullptr)
            {
                continue;
            }
            const Vector2 a = ToScreen(camera, OutputPosition(Shifted(*from, moving, moveOffset), link.from.pin, style));
            const Vector2 b = ToScreen(camera, InputPosition(Shifted(*to, moving, moveOffset), link.to.pin, style));
            const Float distance = DistanceToCurve(LinkCurve(a, b, camera.zoom), screen);
            if (distance.Get() <= bestDistance.Get())
            {
                bestDistance = distance;
                best = Hit{HitKind::Link, NodeGraph::InvalidId, 0, link.id};
            }
        }
        return best;
    }

    void NodesInRect(const Graph& graph, const Domain& domain, const Style& style, const Rect& rect, Array<NodeId>& nodes)
    {
        nodes.Clear();
        for (const Node& node : graph.GetNodes())
        {
            if (Overlaps(NodeBounds(node, domain.FindNodeType(node.type.View()), style), rect))
            {
                nodes.Add(node.id);
            }
        }
    }

    Bool Interaction::IsSelected(NodeId id) const noexcept
    {
        return Contains(m_selection.View(), id);
    }

    void Interaction::Select(ArrayView<const NodeId> nodes)
    {
        m_selection.Clear();
        for (const NodeId& node : nodes)
        {
            m_selection.Add(node);
        }
    }

    void Interaction::ClearSelection()
    {
        m_selection.Clear();
    }

    void Interaction::Prune(const Graph& graph)
    {
        m_selection.RemoveAll([&graph](const NodeId& id) { return graph.FindNode(id) == nullptr; });
    }

    Bool Interaction::GetLinkOrigin(PinRef& pin, Bool& fromOutput) const noexcept
    {
        if (m_gesture != Gesture::Linking)
        {
            return false;
        }
        pin = PinRef{m_pressHit.node, m_pressHit.pin};
        fromOutput = m_pressHit.kind == HitKind::Output;
        return true;
    }

    Bool Interaction::GetBoxRect(Rect& rect) const noexcept
    {
        if (m_gesture != Gesture::BoxSelecting)
        {
            return false;
        }
        rect = MakeRect(m_pressScreen, m_mouse);
        return true;
    }

    Bool Interaction::CanLinkTo(const Graph& graph, const Domain& domain, const Hit& hit) const
    {
        if (m_gesture != Gesture::Linking)
        {
            return false;
        }
        const Bool fromOutput = m_pressHit.kind == HitKind::Output;
        if ((fromOutput && hit.kind != HitKind::Input) || (!fromOutput && hit.kind != HitKind::Output))
        {
            return false;
        }
        const PinRef origin{m_pressHit.node, m_pressHit.pin};
        const PinRef other{hit.node, hit.pin};
        // 실제로 이어 보고 판단한다 - 규칙이 모델 한 곳에만 있어야 화면과 모델이 어긋나지 않는다.
        Graph probe = graph;
        return probe.Connect(domain, fromOutput ? origin : other, fromOutput ? other : origin) == NodeGraph::ConnectResult::Connected;
    }

    void Interaction::Update(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input, Events& events)
    {
        m_mouse = input.mouse;

        // 줌과 팬은 손짓 상태와 따로 돈다. 노드를 끄는 중에도 휠로 줌할 수 있다.
        if (input.hovered && input.wheel.Get() != 0.0f)
        {
            ZoomAt(camera, style, input.mouse, camera.zoom.Get() * std::pow(1.15f, input.wheel.Get()));
        }
        if (input.rightPressed && input.hovered)
        {
            m_rightPress = input.mouse;
            m_rightDragged = false;
        }
        if ((input.rightDown || input.middleDown) && (input.mouseDelta.x.Get() != 0.0f || input.mouseDelta.y.Get() != 0.0f))
        {
            if (input.middleDown || Length(Sub(input.mouse, m_rightPress)) > style.dragThreshold.Get())
            {
                m_rightDragged = m_rightDragged || input.rightDown;
                camera.pan = Sub(camera.pan, Scale(input.mouseDelta, 1.0f / camera.zoom.Get()));
            }
        }
        if (input.rightReleased && input.hovered && !m_rightDragged && m_gesture == Gesture::Idle)
        {
            events.contextMenu = true;
            events.contextHit = Pick(graph, domain, style, camera, input.mouse);
            events.contextPoint = ToGraph(camera, input.mouse);
        }

        switch (m_gesture)
        {
        case Gesture::Idle:
            if (!input.leftPressed || !input.hovered)
            {
                break;
            }
            m_pressScreen = input.mouse;
            m_pressHit = Pick(graph, domain, style, camera, input.mouse);
            m_moveOffset = Vector2{};
            if (m_pressHit.kind == HitKind::Link && input.alt)
            {
                events.disconnect = true;
                events.disconnectLink = m_pressHit.link;
                break;
            }
            if (m_pressHit.kind == HitKind::Input && input.alt)
            {
                if (const NodeGraph::Link* link = graph.FindLinkInto(PinRef{m_pressHit.node, m_pressHit.pin}))
                {
                    events.disconnect = true;
                    events.disconnectLink = link->id;
                }
                break;
            }
            if (m_pressHit.kind == HitKind::Input || m_pressHit.kind == HitKind::Output)
            {
                m_gesture = Gesture::Linking;
                break;
            }
            if (m_pressHit.kind == HitKind::Node)
            {
                m_pressAdds = input.ctrl || input.shift;
                if (m_pressAdds)
                {
                    if (IsSelected(m_pressHit.node))
                    {
                        const NodeId id = m_pressHit.node;
                        m_selection.RemoveAll([id](const NodeId& node) { return node.Get() == id.Get(); });
                    }
                    else
                    {
                        m_selection.Add(m_pressHit.node);
                    }
                    events.selectionChanged = true;
                }
                else if (!IsSelected(m_pressHit.node))
                {
                    m_selection.Clear();
                    m_selection.Add(m_pressHit.node);
                    events.selectionChanged = true;
                }
                m_gesture = Gesture::Pressing;
                break;
            }
            // 빈 곳이다. 상자로 고르기를 준비한다. 조합키가 없으면 지금 고른 것을 버린다.
            m_pressAdds = input.ctrl || input.shift;
            if (!m_pressAdds && !m_selection.IsEmpty())
            {
                m_selection.Clear();
                events.selectionChanged = true;
            }
            m_gesture = Gesture::Pressing;
            break;

        case Gesture::Pressing:
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
                break;
            }
            if (Length(Sub(input.mouse, m_pressScreen)) > style.dragThreshold.Get())
            {
                m_gesture = m_pressHit.kind == HitKind::Node ? Gesture::MovingNodes : Gesture::BoxSelecting;
                if (m_gesture == Gesture::MovingNodes && !IsSelected(m_pressHit.node))
                {
                    // 조합키로 방금 고름에서 뺀 노드를 끌면 그것을 다시 넣어 옮긴다.
                    m_selection.Add(m_pressHit.node);
                    events.selectionChanged = true;
                }
                // 끌기가 된 프레임부터 노드가 마우스를 따라간다 - 한 프레임 늦으면 손에서 미끄러져 보인다.
                m_moveOffset = Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get());
            }
            break;

        case Gesture::MovingNodes:
            m_moveOffset = Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get());
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
            }
            break;

        case Gesture::Linking:
        case Gesture::BoxSelecting:
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
            }
            break;

        case Gesture::Panning:
            m_gesture = Gesture::Idle;
            break;
        }
    }

    void Interaction::Release(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, const Input& input, Events& events)
    {
        switch (m_gesture)
        {
        case Gesture::Pressing:
            // 끌지 않고 놓았다. 고른 노드 하나를 누르면 그것만 남긴다(여럿을 고른 채 하나를 누른 것).
            if (m_pressHit.kind == HitKind::Node && !m_pressAdds && m_selection.Size() > 1)
            {
                m_selection.Clear();
                m_selection.Add(m_pressHit.node);
                events.selectionChanged = true;
            }
            break;
        case Gesture::MovingNodes:
            m_moveOffset = Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get());
            if (m_moveOffset.x.Get() != 0.0f || m_moveOffset.y.Get() != 0.0f)
            {
                events.moved = true;
                events.moveDelta = m_moveOffset;
            }
            break;
        case Gesture::Linking:
        {
            const Hit hit = Pick(graph, domain, style, camera, input.mouse);
            const Bool fromOutput = m_pressHit.kind == HitKind::Output;
            const PinRef origin{m_pressHit.node, m_pressHit.pin};
            if ((fromOutput && hit.kind == HitKind::Input) || (!fromOutput && hit.kind == HitKind::Output))
            {
                events.connect = true;
                events.connectFrom = fromOutput ? origin : PinRef{hit.node, hit.pin};
                events.connectTo = fromOutput ? PinRef{hit.node, hit.pin} : origin;
            }
            else if (hit.kind == HitKind::None)
            {
                events.linkDropped = true;
                events.droppedPin = origin;
                events.droppedFromOutput = fromOutput;
                events.dropPoint = ToGraph(camera, input.mouse);
            }
            break;
        }
        case Gesture::BoxSelecting:
        {
            Array<NodeId> inside;
            NodesInRect(graph, domain, style, MakeRect(ToGraph(camera, m_pressScreen), ToGraph(camera, input.mouse)), inside);
            for (const NodeId& node : inside)
            {
                if (!IsSelected(node))
                {
                    m_selection.Add(node);
                }
            }
            events.selectionChanged = true;
            break;
        }
        default:
            break;
        }
        m_gesture = Gesture::Idle;
        m_moveOffset = Vector2{};
    }
}
