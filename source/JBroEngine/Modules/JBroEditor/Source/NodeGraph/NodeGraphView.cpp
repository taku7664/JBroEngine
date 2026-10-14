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

        Bool Contains(ArrayView<const UInt64> items, UInt64 id)
        {
            for (const UInt64& item : items)
            {
                if (item.Get() == id.Get())
                {
                    return true;
                }
            }
            return false;
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

        Bool Inside(const Rect& inner, const Rect& outer)
        {
            return inner.min.x.Get() >= outer.min.x.Get() && inner.min.y.Get() >= outer.min.y.Get()
                && inner.max.x.Get() <= outer.max.x.Get() && inner.max.y.Get() <= outer.max.y.Get();
        }

        void Grow(Rect& bounds, const Rect& rect, Bool& any)
        {
            if (!any)
            {
                bounds = rect;
                any = true;
                return;
            }
            bounds.min = Vector2{std::min(bounds.min.x.Get(), rect.min.x.Get()), std::min(bounds.min.y.Get(), rect.min.y.Get())};
            bounds.max = Vector2{std::max(bounds.max.x.Get(), rect.max.x.Get()), std::max(bounds.max.y.Get(), rect.max.y.Get())};
        }

        // 링크 한 줄의 화면 곡선들이다. 미리 옮긴 노드와 경유점을 더한다.
        void ScreenPath(const Graph& graph, const Style& style, const Camera& camera, const NodeGraph::Link& link, const Preview& preview,
            Array<Curve>& curves)
        {
            curves.Clear();
            const Node* from = graph.FindNode(link.from.node);
            const Node* to = graph.FindNode(link.to.node);
            if (from == nullptr || to == nullptr)
            {
                return;
            }
            const Vector2 a = ToScreen(camera, OutputPosition(PlacedNode(*from, preview), link.from.pin, style));
            const Vector2 b = ToScreen(camera, InputPosition(PlacedNode(*to, preview), link.to.pin, style));
            Array<Vector2> points;
            PlacedWaypoints(link, preview, points);
            for (Vector2& point : points)
            {
                point = ToScreen(camera, point);
            }
            LinkPath(a, points.View(), b, camera.zoom, curves);
        }
    }

    void ZoomAt(Camera& camera, const Style& style, Vector2 screen, Float zoom)
    {
        ViewCamera::ZoomAt(camera, screen, zoom, style.minZoom, style.maxZoom);
    }

    void Frame(Camera& camera, const Style& style, const Graph& graph, const Domain& domain, Vector2 canvasSize,
        ArrayView<const UInt64> items, Bool immediate)
    {
        Bool any = false;
        Rect bounds;
        for (const Node& node : graph.GetNodes())
        {
            if (items.IsEmpty() || Contains(items, node.id))
            {
                Grow(bounds, NodeBounds(node, domain.FindNodeType(node.type.View()), style), any);
            }
        }
        for (const Comment& comment : graph.GetComments())
        {
            if (items.IsEmpty() || Contains(items, comment.id))
            {
                Grow(bounds, CommentBounds(comment), any);
            }
        }
        if (!any)
        {
            camera.goalZoom = 1.0f;
            camera.goalPan = Vector2{};
            if (immediate)
            {
                SetCamera(camera, camera.goalPan, camera.goalZoom);
            }
            return;
        }
        // 작은 그래프를 1 보다 크게 키우지 않는다 - 노드 둘을 맞춘다고 화면을 가득 채우면 글자가 부풀어 보인다.
        ViewCamera::Fit(camera, bounds, canvasSize, 40.0f, style.minZoom, std::min(1.0f, style.maxZoom.Get()), immediate);
    }

    void CenterOn(Camera& camera, Vector2 canvasSize, Vector2 graphPoint)
    {
        const float zoom = camera.goalZoom.Get() <= 0.0f ? 1.0f : camera.goalZoom.Get();
        camera.goalPan = Vector2{graphPoint.x.Get() - canvasSize.x.Get() * 0.5f / zoom, graphPoint.y.Get() - canvasSize.y.Get() * 0.5f / zoom};
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

    Rect CommentBounds(const Comment& comment)
    {
        Rect rect;
        rect.min = comment.position;
        rect.max = Add(comment.position, comment.size);
        return rect;
    }

    Bool ContentBounds(const Graph& graph, const Domain& domain, const Style& style, Rect& bounds)
    {
        Bool any = false;
        for (const Node& node : graph.GetNodes())
        {
            Grow(bounds, NodeBounds(node, domain.FindNodeType(node.type.View()), style), any);
        }
        for (const Comment& comment : graph.GetComments())
        {
            Grow(bounds, CommentBounds(comment), any);
        }
        return any;
    }

    UInt32 InputFieldComponents(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, const Node& node, std::size_t pin)
    {
        if (camera.zoom.Get() < style.fieldMinZoom.Get() || pin >= node.inputs.Size())
        {
            return 0;
        }
        const NodeTypeInfo* type = domain.FindNodeType(node.type.View());
        if (type == nullptr || pin >= type->inputs.Size() || graph.FindLinkInto(PinRef{node.id, pin}) != nullptr)
        {
            return 0;
        }
        const NodeGraph::PinTypeInfo* pinType = domain.FindPinType(type->inputs[pin].type);
        if (pinType == nullptr || pinType->components.Get() == 0 || pinType->components.Get() > 4)
        {
            return 0;
        }
        return pinType->components;
    }

    Rect InputFieldBounds(const Node& node, std::size_t pin, UInt32 components, const Style& style)
    {
        const Vector2 at = InputPosition(node, pin, style);
        const float width = style.fieldWidth.Get() * static_cast<float>(components.Get());
        const float half = (style.rowHeight.Get() - 4.0f) * 0.5f;
        const float right = at.x.Get() - style.fieldGap.Get();
        Rect rect;
        rect.min = Vector2{right - width, at.y.Get() - half};
        rect.max = Vector2{right, at.y.Get() + half};
        return rect;
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

    void LinkPath(Vector2 fromScreen, ArrayView<const Vector2> waypointsScreen, Vector2 toScreen, Float zoom, Array<Curve>& curves)
    {
        curves.Clear();
        Vector2 previous = fromScreen;
        for (const Vector2& point : waypointsScreen)
        {
            curves.Add(LinkCurve(previous, point, zoom));
            previous = point;
        }
        curves.Add(LinkCurve(previous, toScreen, zoom));
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

    Bool LayoutMinimap(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, Vector2 canvasSize, Minimap& minimap)
    {
        const float width = style.minimapWidth.Get();
        const float height = style.minimapHeight.Get();
        const float margin = style.minimapMargin.Get();
        // 캔버스가 미니맵의 두 배도 안 되면 미니맵이 그래프를 가린다.
        if (!style.showMinimap || canvasSize.x.Get() < width * 2.0f || canvasSize.y.Get() < height * 2.0f)
        {
            return false;
        }
        Rect content;
        if (!ContentBounds(graph, domain, style, content))
        {
            return false;
        }
        // 지금 보이는 곳도 넣는다 - 내용 밖으로 나가 있어도 미니맵에서 내가 어디 있는지 보인다.
        Rect view;
        view.min = ToGraph(camera, camera.origin);
        view.max = ToGraph(camera, Add(camera.origin, canvasSize));
        Bool any = true;
        Grow(content, view, any);
        const float contentWidth = std::max(1.0f, content.max.x.Get() - content.min.x.Get());
        const float contentHeight = std::max(1.0f, content.max.y.Get() - content.min.y.Get());
        const float inner = 6.0f;
        const float scale = std::min((width - inner * 2.0f) / contentWidth, (height - inner * 2.0f) / contentHeight);
        minimap.screen.max = Vector2{camera.origin.x.Get() + canvasSize.x.Get() - margin, camera.origin.y.Get() + canvasSize.y.Get() - margin};
        minimap.screen.min = Vector2{minimap.screen.max.x.Get() - width, minimap.screen.max.y.Get() - height};
        minimap.content = content;
        minimap.scale = scale;
        minimap.offset = Vector2{(width - contentWidth * scale) * 0.5f, (height - contentHeight * scale) * 0.5f};
        return true;
    }

    Vector2 MinimapToGraph(const Minimap& minimap, Vector2 screen)
    {
        const float scale = minimap.scale.Get() <= 0.0f ? 1.0f : minimap.scale.Get();
        return Vector2{minimap.content.min.x.Get() + (screen.x.Get() - minimap.screen.min.x.Get() - minimap.offset.x.Get()) / scale,
            minimap.content.min.y.Get() + (screen.y.Get() - minimap.screen.min.y.Get() - minimap.offset.y.Get()) / scale};
    }

    Vector2 GraphToMinimap(const Minimap& minimap, Vector2 graphPoint)
    {
        return Vector2{minimap.screen.min.x.Get() + minimap.offset.x.Get() + (graphPoint.x.Get() - minimap.content.min.x.Get()) * minimap.scale.Get(),
            minimap.screen.min.y.Get() + minimap.offset.y.Get() + (graphPoint.y.Get() - minimap.content.min.y.Get()) * minimap.scale.Get()};
    }

    Node PlacedNode(const Node& node, const Preview& preview)
    {
        Node placed;
        placed.id = node.id;
        placed.position = Contains(preview.moving, node.id) ? Add(node.position, preview.moveOffset) : node.position;
        return placed;
    }

    Comment PlacedComment(const Comment& comment, const Preview& preview)
    {
        Comment placed;
        placed.id = comment.id;
        placed.position = Contains(preview.moving, comment.id) ? Add(comment.position, preview.moveOffset) : comment.position;
        placed.size = preview.resizing.Get() == comment.id.Get() ? preview.resizedSize : comment.size;
        placed.color = comment.color;
        return placed;
    }

    void PlacedWaypoints(const NodeGraph::Link& link, const Preview& preview, Array<Vector2>& points)
    {
        points.Clear();
        const Bool carried = Contains(preview.moving, link.from.node) && Contains(preview.moving, link.to.node);
        for (std::size_t index = 0; index < link.waypoints.Size(); ++index)
        {
            Vector2 point = link.waypoints[index];
            if (carried)
            {
                point = Add(point, preview.moveOffset);
            }
            if (preview.waypointLink.Get() == link.id.Get() && preview.waypointIndex == index)
            {
                point = preview.waypointPoint;
            }
            points.Add(point);
        }
    }

    Hit Pick(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, Vector2 screen,
        const Preview& preview, Vector2 canvasSize)
    {
        // 미니맵은 맨 위에 덮인다.
        if (canvasSize.x.Get() > 0.0f)
        {
            Minimap minimap;
            if (LayoutMinimap(graph, domain, style, camera, canvasSize, minimap) && minimap.screen.Contains(screen))
            {
                Hit hit;
                hit.kind = HitKind::Minimap;
                return hit;
            }
        }
        const ArrayView<const Node> nodes = graph.GetNodes();
        const float pinRadius = style.pinPickRadius.Get();
        // 핀이 먼저다 - 노드 가장자리에 걸쳐 있어 노드 판정이 먼저면 핀을 잡을 수 없다. 뒤에 그린 노드가 위에 있다.
        for (std::size_t reverse = nodes.Size(); reverse > 0; --reverse)
        {
            const Node node = PlacedNode(nodes[reverse - 1], preview);
            const NodeTypeInfo* type = domain.FindNodeType(nodes[reverse - 1].type.View());
            if (type == nullptr)
            {
                continue;
            }
            for (std::size_t pin = 0; pin < type->outputs.Size(); ++pin)
            {
                if (Length(Sub(ToScreen(camera, OutputPosition(node, pin, style)), screen)) <= pinRadius)
                {
                    Hit hit;
                    hit.kind = HitKind::Output;
                    hit.node = node.id;
                    hit.pin = pin;
                    return hit;
                }
            }
            for (std::size_t pin = 0; pin < type->inputs.Size(); ++pin)
            {
                if (Length(Sub(ToScreen(camera, InputPosition(node, pin, style)), screen)) <= pinRadius)
                {
                    Hit hit;
                    hit.kind = HitKind::Input;
                    hit.node = node.id;
                    hit.pin = pin;
                    return hit;
                }
            }
        }
        const Vector2 graphPoint = ToGraph(camera, screen);
        // 값 칸은 노드 밖(핀 왼쪽)에 있어 노드보다 먼저 본다 - 왼쪽 노드 위에 걸쳐 있어도 칸이 위에 그려진다.
        for (std::size_t reverse = nodes.Size(); reverse > 0; --reverse)
        {
            const Node& source = nodes[reverse - 1];
            const Node node = PlacedNode(source, preview);
            for (std::size_t pin = 0; pin < source.inputs.Size(); ++pin)
            {
                const UInt32 components = InputFieldComponents(graph, domain, style, camera, source, pin);
                if (components.Get() != 0 && InputFieldBounds(node, pin, components, style).Contains(graphPoint))
                {
                    Hit hit;
                    hit.kind = HitKind::Field;
                    hit.node = source.id;
                    hit.pin = pin;
                    return hit;
                }
            }
        }
        for (std::size_t reverse = nodes.Size(); reverse > 0; --reverse)
        {
            const Node node = PlacedNode(nodes[reverse - 1], preview);
            if (NodeBounds(node, domain.FindNodeType(nodes[reverse - 1].type.View()), style).Contains(graphPoint))
            {
                Hit hit;
                hit.kind = HitKind::Node;
                hit.node = node.id;
                return hit;
            }
        }
        Array<Vector2> points;
        for (const NodeGraph::Link& link : graph.GetLinks())
        {
            PlacedWaypoints(link, preview, points);
            for (std::size_t index = 0; index < points.Size(); ++index)
            {
                if (Length(Sub(ToScreen(camera, points[index]), screen)) <= style.waypointPickRadius.Get())
                {
                    Hit hit;
                    hit.kind = HitKind::Waypoint;
                    hit.link = link.id;
                    hit.waypoint = index;
                    return hit;
                }
            }
        }
        Float bestDistance = style.linkPickDistance;
        Hit best;
        Array<Curve> curves;
        for (const NodeGraph::Link& link : graph.GetLinks())
        {
            ScreenPath(graph, style, camera, link, preview, curves);
            for (const Curve& curve : curves)
            {
                const Float distance = DistanceToCurve(curve, screen);
                if (distance.Get() <= bestDistance.Get())
                {
                    bestDistance = distance;
                    best = Hit{};
                    best.kind = HitKind::Link;
                    best.link = link.id;
                }
            }
        }
        if (best.kind != HitKind::None)
        {
            return best;
        }
        const ArrayView<const Comment> comments = graph.GetComments();
        for (std::size_t reverse = comments.Size(); reverse > 0; --reverse)
        {
            const Comment comment = PlacedComment(comments[reverse - 1], preview);
            const Vector2 topLeft = ToScreen(camera, comment.position);
            const Vector2 bottomRight = ToScreen(camera, Add(comment.position, comment.size));
            const float handle = style.commentHandle.Get();
            if (screen.x.Get() >= bottomRight.x.Get() - handle && screen.x.Get() <= bottomRight.x.Get()
                && screen.y.Get() >= bottomRight.y.Get() - handle && screen.y.Get() <= bottomRight.y.Get())
            {
                Hit hit;
                hit.kind = HitKind::CommentResize;
                hit.comment = comment.id;
                return hit;
            }
            const float headerBottom = topLeft.y.Get() + style.commentHeaderHeight.Get() * camera.zoom.Get();
            if (screen.x.Get() >= topLeft.x.Get() && screen.x.Get() <= bottomRight.x.Get()
                && screen.y.Get() >= topLeft.y.Get() && screen.y.Get() <= headerBottom)
            {
                Hit hit;
                hit.kind = HitKind::CommentHeader;
                hit.comment = comment.id;
                return hit;
            }
        }
        // 주석의 몸은 바닥과 같다 - 그 위에서 끌면 상자로 고른다(언리얼 블루프린트와 같다).
        return Hit{};
    }

    void ItemsInRect(const Graph& graph, const Domain& domain, const Style& style, const Rect& rect, Array<UInt64>& items)
    {
        items.Clear();
        for (const Node& node : graph.GetNodes())
        {
            if (Overlaps(NodeBounds(node, domain.FindNodeType(node.type.View()), style), rect))
            {
                items.Add(node.id);
            }
        }
        for (const Comment& comment : graph.GetComments())
        {
            if (Inside(CommentBounds(comment), rect))
            {
                items.Add(comment.id);
            }
        }
    }

    void ItemsInComment(const Graph& graph, const Domain& domain, const Style& style, const Comment& comment, Array<UInt64>& items)
    {
        items.Clear();
        const Rect bounds = CommentBounds(comment);
        for (const Node& node : graph.GetNodes())
        {
            if (Inside(NodeBounds(node, domain.FindNodeType(node.type.View()), style), bounds))
            {
                items.Add(node.id);
            }
        }
        for (const Comment& other : graph.GetComments())
        {
            if (other.id.Get() != comment.id.Get() && Inside(CommentBounds(other), bounds))
            {
                items.Add(other.id);
            }
        }
    }

    std::size_t WaypointInsertIndex(const Graph& graph, const Style& style, const Camera& camera, const NodeGraph::Link& link, Vector2 screen)
    {
        Array<Curve> curves;
        ScreenPath(graph, style, camera, link, Preview{}, curves);
        std::size_t best = 0;
        float bestDistance = 1.0e30f;
        for (std::size_t index = 0; index < curves.Size(); ++index)
        {
            const float distance = DistanceToCurve(curves[index], screen).Get();
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = index;
            }
        }
        // 곡선 i 는 경유점 i-1 과 i 사이다 - 그 사이에 끼우면 새 점의 차례가 i 다.
        return best;
    }

    Bool Interaction::IsSelected(UInt64 id) const noexcept
    {
        return Contains(m_selection.View(), id);
    }

    Bool Interaction::IsMoving(UInt64 id) const noexcept
    {
        return m_gesture == Gesture::MovingNodes && Contains(m_moving.View(), id);
    }

    void Interaction::Select(ArrayView<const UInt64> items)
    {
        m_selection.Clear();
        for (const UInt64& item : items)
        {
            m_selection.Add(item);
        }
    }

    void Interaction::ClearSelection()
    {
        m_selection.Clear();
    }

    void Interaction::Prune(const Graph& graph)
    {
        m_selection.RemoveAll([&graph](const UInt64& id) { return graph.FindNode(id) == nullptr && graph.FindComment(id) == nullptr; });
    }

    Preview Interaction::GetPreview() const noexcept
    {
        Preview preview;
        if (m_gesture == Gesture::MovingNodes)
        {
            preview.moving = m_moving.View();
            preview.moveOffset = m_moveOffset;
        }
        if (m_gesture == Gesture::MovingWaypoint)
        {
            preview.waypointLink = m_pressHit.link;
            preview.waypointIndex = m_pressHit.waypoint;
            preview.waypointPoint = m_waypointPoint;
        }
        if (m_gesture == Gesture::ResizingComment)
        {
            preview.resizing = m_pressHit.comment;
            preview.resizedSize = m_resizedSize;
        }
        return preview;
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

    void Interaction::BeginMove(const Graph& graph, const Domain& domain, const Style& style)
    {
        // 고른 것과, 고른 주석 안에 다 들어가는 것이 함께 간다. 끌기를 시작할 때 한 번 정한다 - 끄는 동안 주석 밖으로 나간 노드도 계속 따라온다.
        m_moving.Clear();
        Array<UInt64> inside;
        for (const UInt64& id : m_selection)
        {
            if (!Contains(m_moving.View(), id))
            {
                m_moving.Add(id);
            }
            if (const Comment* comment = graph.FindComment(id))
            {
                ItemsInComment(graph, domain, style, *comment, inside);
                for (const UInt64& item : inside)
                {
                    if (!Contains(m_moving.View(), item))
                    {
                        m_moving.Add(item);
                    }
                }
            }
        }
    }

    void Interaction::FollowMinimap(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input)
    {
        Minimap minimap;
        if (LayoutMinimap(graph, domain, style, camera, input.canvasSize, minimap))
        {
            // 누른 미니맵 자리가 화면 가운데로 온다. 가려는 카메라만 옮겨 부드럽게 간다.
            CenterOn(camera, input.canvasSize, MinimapToGraph(minimap, input.mouse));
        }
    }

    void Interaction::Update(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input, Events& events)
    {
        m_mouse = input.mouse;

        // 줌과 팬은 손짓 상태와 따로 돈다. 노드를 끄는 중에도 휠로 줌할 수 있다.
        if (input.hovered && input.wheel.Get() != 0.0f)
        {
            ZoomAt(camera, style, input.mouse, camera.goalZoom.Get() * std::pow(1.15f, input.wheel.Get()));
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
                // 끈 픽셀을 가려는 줌으로 그래프 길이로 바꿔 가려는 카메라를 민다(캔버스 뷰와 같다).
                ViewCamera::PanByPixels(camera, input.mouseDelta);
            }
        }
        if (input.rightReleased && input.hovered && !m_rightDragged && m_gesture == Gesture::Idle)
        {
            events.contextMenu = true;
            events.contextHit = Pick(graph, domain, style, camera, input.mouse, Preview{}, input.canvasSize);
            events.contextPoint = ToGraph(camera, input.mouse);
        }

        switch (m_gesture)
        {
        case Gesture::Idle:
            if (input.leftPressed && input.hovered)
            {
                Press(graph, domain, style, camera, input, events);
            }
            break;

        case Gesture::Pressing:
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
                break;
            }
            if (Length(Sub(input.mouse, m_pressScreen)) > style.dragThreshold.Get())
            {
                const Bool item = m_pressHit.kind == HitKind::Node || m_pressHit.kind == HitKind::CommentHeader;
                m_gesture = item ? Gesture::MovingNodes : Gesture::BoxSelecting;
                if (m_gesture == Gesture::MovingNodes)
                {
                    const UInt64 pressed = m_pressHit.kind == HitKind::Node ? m_pressHit.node : m_pressHit.comment;
                    if (!IsSelected(pressed))
                    {
                        // 조합키로 방금 고름에서 뺀 것을 끌면 그것을 다시 넣어 옮긴다.
                        m_selection.Add(pressed);
                        events.selectionChanged = true;
                    }
                    BeginMove(graph, domain, style);
                    // 끌기가 된 프레임부터 노드가 마우스를 따라간다 - 한 프레임 늦으면 손에서 미끄러져 보인다.
                    m_moveOffset = Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get());
                }
            }
            break;

        case Gesture::MovingNodes:
            m_moveOffset = Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get());
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
            }
            break;

        case Gesture::MovingWaypoint:
            m_waypointPoint = Add(m_waypointStart, Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get()));
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
            }
            break;

        case Gesture::ResizingComment:
        {
            const Vector2 grown = Add(m_resizeStart, Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get()));
            m_resizedSize = Vector2{std::max(grown.x.Get(), style.commentMinWidth.Get()), std::max(grown.y.Get(), style.commentMinHeight.Get())};
            if (input.leftReleased || !input.leftDown)
            {
                Release(graph, domain, style, camera, input, events);
            }
            break;
        }

        case Gesture::Minimap:
            FollowMinimap(graph, domain, style, camera, input);
            if (input.leftReleased || !input.leftDown)
            {
                m_gesture = Gesture::Idle;
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

    void Interaction::Press(const Graph& graph, const Domain& domain, const Style& style, Camera& camera, const Input& input, Events& events)
    {
        m_pressScreen = input.mouse;
        m_pressHit = Pick(graph, domain, style, camera, input.mouse, Preview{}, input.canvasSize);
        m_moveOffset = Vector2{};
        switch (m_pressHit.kind)
        {
        case HitKind::Minimap:
            m_gesture = Gesture::Minimap;
            FollowMinimap(graph, domain, style, camera, input);
            return;
        case HitKind::Field:
            // 값 칸이 마우스를 받는다. 고름도 손짓도 건드리지 않는다.
            return;
        case HitKind::Link:
            if (input.alt)
            {
                events.disconnect = true;
                events.disconnectLink = m_pressHit.link;
                return;
            }
            if (input.leftDoubleClicked)
            {
                // 링크를 두 번 누르면 그 자리에 경유점이 선다(언리얼 블루프린트의 경유 노드와 같은 손짓).
                if (const NodeGraph::Link* link = graph.FindLink(m_pressHit.link))
                {
                    events.waypointAdded = true;
                    events.waypointLink = link->id;
                    events.waypointIndex = WaypointInsertIndex(graph, style, camera, *link, input.mouse);
                    events.waypointPoint = ToGraph(camera, input.mouse);
                }
                return;
            }
            break;
        case HitKind::Waypoint:
        {
            if (input.alt)
            {
                events.waypointRemoved = true;
                events.waypointLink = m_pressHit.link;
                events.waypointIndex = m_pressHit.waypoint;
                return;
            }
            const NodeGraph::Link* link = graph.FindLink(m_pressHit.link);
            if (link == nullptr || m_pressHit.waypoint >= link->waypoints.Size())
            {
                return;
            }
            m_waypointStart = link->waypoints[m_pressHit.waypoint];
            m_waypointPoint = m_waypointStart;
            m_gesture = Gesture::MovingWaypoint;
            return;
        }
        case HitKind::CommentResize:
            if (const Comment* comment = graph.FindComment(m_pressHit.comment))
            {
                m_resizeStart = comment->size;
                m_resizedSize = comment->size;
                m_gesture = Gesture::ResizingComment;
            }
            return;
        case HitKind::Input:
            if (input.alt)
            {
                if (const NodeGraph::Link* link = graph.FindLinkInto(PinRef{m_pressHit.node, m_pressHit.pin}))
                {
                    events.disconnect = true;
                    events.disconnectLink = link->id;
                }
                return;
            }
            m_gesture = Gesture::Linking;
            return;
        case HitKind::Output:
            m_gesture = Gesture::Linking;
            return;
        case HitKind::Node:
        case HitKind::CommentHeader:
        {
            const UInt64 pressed = m_pressHit.kind == HitKind::Node ? m_pressHit.node : m_pressHit.comment;
            m_pressAdds = input.ctrl || input.shift;
            if (m_pressAdds)
            {
                if (IsSelected(pressed))
                {
                    m_selection.RemoveAll([pressed](const UInt64& item) { return item.Get() == pressed.Get(); });
                }
                else
                {
                    m_selection.Add(pressed);
                }
                events.selectionChanged = true;
            }
            else if (!IsSelected(pressed))
            {
                m_selection.Clear();
                m_selection.Add(pressed);
                events.selectionChanged = true;
            }
            if (input.leftDoubleClicked)
            {
                if (m_pressHit.kind == HitKind::Node)
                {
                    events.nodeDoubleClicked = true;
                    events.doubleClickedNode = pressed;
                }
                else
                {
                    events.commentDoubleClicked = true;
                    events.doubleClickedComment = pressed;
                }
            }
            m_gesture = Gesture::Pressing;
            return;
        }
        default:
            break;
        }
        // 빈 곳이다(주석의 몸도). 상자로 고르기를 준비한다. 조합키가 없으면 지금 고른 것을 버린다.
        m_pressAdds = input.ctrl || input.shift;
        if (!m_pressAdds && !m_selection.IsEmpty())
        {
            m_selection.Clear();
            events.selectionChanged = true;
        }
        m_pressHit = Hit{};
        m_gesture = Gesture::Pressing;
    }

    void Interaction::Release(const Graph& graph, const Domain& domain, const Style& style, const Camera& camera, const Input& input, Events& events)
    {
        switch (m_gesture)
        {
        case Gesture::Pressing:
            // 끌지 않고 놓았다. 고른 것 하나를 누르면 그것만 남긴다(여럿을 고른 채 하나를 누른 것).
            if ((m_pressHit.kind == HitKind::Node || m_pressHit.kind == HitKind::CommentHeader) && !m_pressAdds && m_selection.Size() > 1)
            {
                m_selection.Clear();
                m_selection.Add(m_pressHit.kind == HitKind::Node ? m_pressHit.node : m_pressHit.comment);
                events.selectionChanged = true;
            }
            break;
        case Gesture::MovingNodes:
            m_moveOffset = Scale(Sub(input.mouse, m_pressScreen), 1.0f / camera.zoom.Get());
            if (m_moveOffset.x.Get() != 0.0f || m_moveOffset.y.Get() != 0.0f)
            {
                events.moved = true;
                events.moveDelta = m_moveOffset;
                events.movedItems = m_moving;
            }
            break;
        case Gesture::MovingWaypoint:
            if (m_waypointPoint.x.Get() != m_waypointStart.x.Get() || m_waypointPoint.y.Get() != m_waypointStart.y.Get())
            {
                events.waypointMoved = true;
                events.waypointLink = m_pressHit.link;
                events.waypointIndex = m_pressHit.waypoint;
                events.waypointPoint = m_waypointPoint;
            }
            break;
        case Gesture::ResizingComment:
            if (m_resizedSize.x.Get() != m_resizeStart.x.Get() || m_resizedSize.y.Get() != m_resizeStart.y.Get())
            {
                events.commentResized = true;
                events.resizedComment = m_pressHit.comment;
                events.resizedSize = m_resizedSize;
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
            else if (hit.kind == HitKind::None || hit.kind == HitKind::CommentHeader)
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
            Array<UInt64> inside;
            ItemsInRect(graph, domain, style, MakeRect(ToGraph(camera, m_pressScreen), ToGraph(camera, input.mouse)), inside);
            for (const UInt64& item : inside)
            {
                if (!IsSelected(item))
                {
                    m_selection.Add(item);
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
        m_moving.Clear();
    }
}
