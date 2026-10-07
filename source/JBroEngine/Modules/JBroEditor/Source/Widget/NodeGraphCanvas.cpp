#include <JBro/Editor/Widget/NodeGraphCanvas.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/ItemLocator.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace JBro::Widget
{
    namespace
    {
        using NodeGraphView::Hit;
        using NodeGraphView::HitKind;

        ImVec2 ToIm(Vector2 value)
        {
            return ImVec2(value.x.Get(), value.y.Get());
        }

        ImU32 ToU32(const ImVec4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        ImVec4 PinColor(const NodeGraph::Domain& domain, NodeGraph::PinType type)
        {
            const NodeGraph::PinTypeInfo* info = domain.FindPinType(type);
            if (info == nullptr)
            {
                return EditorTheme::InkMuted;
            }
            return ImVec4(info->color.R.Get(), info->color.G.Get(), info->color.B.Get(), info->color.A.Get());
        }

        ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        // 머리의 색이다. 노드가 내는 값의 핀 타입 색을 패널 면에 섞는다 - 같은 값을 내는 노드끼리 한눈에 묶인다.
        ImVec4 HeaderColor(const NodeGraph::Domain& domain, const NodeGraph::NodeTypeInfo* type)
        {
            if (type == nullptr)
            {
                return Mix(EditorTheme::Raised, EditorTheme::Danger, 0.55f);
            }
            if (!type->outputs.IsEmpty())
            {
                return Mix(EditorTheme::Raised, PinColor(domain, type->outputs[0].type), 0.35f);
            }
            if (!type->inputs.IsEmpty())
            {
                return Mix(EditorTheme::Raised, PinColor(domain, type->inputs[0].type), 0.35f);
            }
            return EditorTheme::Raised;
        }

        Bool IsMoving(const NodeGraphCanvasState& state, NodeGraph::NodeId id)
        {
            return state.interaction.GetGesture() == NodeGraphView::Gesture::MovingNodes && state.interaction.IsSelected(id);
        }

        NodeGraph::Node Placed(const NodeGraphCanvasState& state, const NodeGraph::Node& node)
        {
            NodeGraph::Node placed;
            placed.id = node.id;
            placed.position = node.position;
            if (IsMoving(state, node.id))
            {
                const Vector2 offset = state.interaction.GetMoveOffset();
                placed.position = Vector2{node.position.x.Get() + offset.x.Get(), node.position.y.Get() + offset.y.Get()};
            }
            return placed;
        }

        void DrawGrid(ImDrawList& draw, const NodeGraphCanvasState& state, ImVec2 min, ImVec2 max)
        {
            const float step = state.style.gridStep.Get() * state.camera.zoom.Get();
            if (step < 6.0f)
            {
                return;
            }
            const ImU32 minor = ToU32(EditorTheme::Fade(EditorTheme::Line, 0.55f));
            const ImU32 major = ToU32(EditorTheme::Line);
            // 그래프 자리 0 이 지나는 곳에서 시작해 네 칸마다 굵은 선이다 - 줌하고 옮겨도 선이 그래프에 붙어 있다.
            const Vector2 originGraph = ViewCamera::ToScreen(state.camera, Vector2{});
            const float startX = originGraph.x.Get() + std::floor((min.x - originGraph.x.Get()) / step) * step;
            const float startY = originGraph.y.Get() + std::floor((min.y - originGraph.y.Get()) / step) * step;
            for (float x = startX; x < max.x; x += step)
            {
                const long index = std::lround((x - originGraph.x.Get()) / step);
                draw.AddLine(ImVec2(x, min.y), ImVec2(x, max.y), index % 4 == 0 ? major : minor);
            }
            for (float y = startY; y < max.y; y += step)
            {
                const long index = std::lround((y - originGraph.y.Get()) / step);
                draw.AddLine(ImVec2(min.x, y), ImVec2(max.x, y), index % 4 == 0 ? major : minor);
            }
        }

        void DrawCurve(ImDrawList& draw, const NodeGraphView::Curve& curve, ImU32 color, float thickness)
        {
            draw.AddBezierCubic(ToIm(curve.points[0]), ToIm(curve.points[1]), ToIm(curve.points[2]), ToIm(curve.points[3]), color, thickness);
        }

        void FormatValue(char* buffer, std::size_t capacity, const NodeGraph::PinValue& value)
        {
            std::snprintf(buffer, capacity, "%.2f", static_cast<double>(value.values[0].Get()));
        }
    }

    void BeginNodeGraphCanvas(const char* id, const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state,
        const ImVec2& size, NodeGraphView::Events& events)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize(std::max(size.x, 32.0f), std::max(size.y, 32.0f));
        const ImVec2 max(min.x + canvasSize.x, min.y + canvasSize.y);
        state.canvasMin = min;
        state.canvasMax = max;
        state.camera.origin = Vector2{min.x, min.y};
        const Bool untouched = state.framed && state.camera.goalZoom.Get() == state.framedZoom.Get()
            && state.camera.goalPan.x.Get() == state.framedPan.x.Get() && state.camera.goalPan.y.Get() == state.framedPan.y.Get();
        const Bool resized = state.framedSize.x != canvasSize.x || state.framedSize.y != canvasSize.y;
        if (!state.framed || (untouched && resized))
        {
            // 처음 열 때는 따라가지 않고 그 자리에 놓는다 - 열자마자 화면이 미끄러져 들어오면 어지럽다.
            NodeGraphView::Frame(state.camera, state.style, graph, domain, Vector2{canvasSize.x, canvasSize.y}, {}, true);
            state.framed = true;
            state.framedSize = canvasSize;
            state.framedPan = state.camera.goalPan;
            state.framedZoom = state.camera.goalZoom;
        }
        ViewCamera::Follow(state.camera, ImGui::GetIO().DeltaTime);

        // 그림 위의 입력 자리다. 세 단추를 다 받아 창이 끌려가지 않게 한다.
        ImGui::SetNextItemAllowOverlap();
        HitArea(id, canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const Bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const Bool active = ImGui::IsItemActive();

        const ImGuiIO& io = ImGui::GetIO();
        NodeGraphView::Input input;
        input.mouse = Vector2{io.MousePos.x, io.MousePos.y};
        input.hovered = hovered;
        input.leftPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        input.leftDown = io.MouseDown[ImGuiMouseButton_Left];
        input.leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        input.leftDoubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        input.rightPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        input.rightDown = (hovered || active) && io.MouseDown[ImGuiMouseButton_Right];
        input.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        input.middleDown = (hovered || active) && io.MouseDown[ImGuiMouseButton_Middle];
        input.mouseDelta = Vector2{io.MouseDelta.x, io.MouseDelta.y};
        input.wheel = hovered ? io.MouseWheel : 0.0f;
        input.ctrl = io.KeyCtrl;
        input.shift = io.KeyShift;
        input.alt = io.KeyAlt;
        state.interaction.Update(graph, domain, state.style, state.camera, input, events);
        state.mouse = input.mouse;
        state.canvasHovered = hovered;
    }

    void EndNodeGraphCanvas(const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state)
    {
        const ImVec2 min = state.canvasMin;
        const ImVec2 max = state.canvasMax;
        NodeGraphView::Input input;
        input.mouse = state.mouse;
        const Bool moving = state.interaction.GetGesture() == NodeGraphView::Gesture::MovingNodes;
        state.hovered = state.canvasHovered
            ? NodeGraphView::Pick(graph, domain, state.style, state.camera, input.mouse,
                  moving ? state.interaction.GetSelection() : ArrayView<const NodeGraph::NodeId>{}, state.interaction.GetMoveOffset())
            : Hit{};

        ImDrawList& draw = *ImGui::GetWindowDrawList();
        draw.PushClipRect(min, max, true);
        draw.AddRectFilled(min, max, ToU32(EditorTheme::ViewportBackground));
        DrawGrid(draw, state, min, max);

        const float zoom = state.camera.zoom.Get();
        const NodeGraphView::Style& style = state.style;
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize() * zoom;
        const Bool drawText = fontSize >= 6.0f;

        // ── 링크 ──
        for (const NodeGraph::Link& link : graph.GetLinks())
        {
            const NodeGraph::Node* from = graph.FindNode(link.from.node);
            const NodeGraph::Node* to = graph.FindNode(link.to.node);
            const NodeGraph::NodeTypeInfo* fromType = from == nullptr ? nullptr : domain.FindNodeType(from->type.View());
            if (from == nullptr || to == nullptr || fromType == nullptr || link.from.pin >= fromType->outputs.Size())
            {
                continue;
            }
            const Vector2 a = ViewCamera::ToScreen(state.camera, NodeGraphView::OutputPosition(Placed(state, *from), link.from.pin, style));
            const Vector2 b = ViewCamera::ToScreen(state.camera, NodeGraphView::InputPosition(Placed(state, *to), link.to.pin, style));
            const Bool lit = state.hovered.kind == HitKind::Link && state.hovered.link.Get() == link.id.Get();
            const ImVec4 color = PinColor(domain, fromType->outputs[link.from.pin].type);
            DrawCurve(draw, NodeGraphView::LinkCurve(a, b, state.camera.zoom), ToU32(lit ? EditorTheme::AccentBright : color),
                std::max(1.5f, (lit ? 3.5f : 2.5f) * zoom));
        }

        // ── 노드 ──
        const float rounding = 6.0f * zoom;
        for (const NodeGraph::Node& source : graph.GetNodes())
        {
            const NodeGraph::Node node = Placed(state, source);
            const NodeGraph::NodeTypeInfo* type = domain.FindNodeType(source.type.View());
            const Rect bounds = NodeGraphView::NodeBounds(node, type, style);
            const ImVec2 topLeft = ToIm(ViewCamera::ToScreen(state.camera, bounds.min));
            const ImVec2 bottomRight = ToIm(ViewCamera::ToScreen(state.camera, bounds.max));
            if (ItemLocator::IsWatching())
            {
                char label[40];
                std::snprintf(label, sizeof(label), "##node%llu", static_cast<unsigned long long>(source.id.Get()));
                ItemLocator::Report(ImGui::GetID(label), ItemLocator::ItemRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y});
            }
            if (bottomRight.x < min.x || topLeft.x > max.x || bottomRight.y < min.y || topLeft.y > max.y)
            {
                continue;
            }
            const float headerBottom = topLeft.y + style.headerHeight.Get() * zoom;
            draw.AddRectFilled(topLeft, bottomRight, ToU32(EditorTheme::Panel), rounding);
            draw.AddRectFilled(topLeft, ImVec2(bottomRight.x, headerBottom), ToU32(HeaderColor(domain, type)), rounding, ImDrawFlags_RoundCornersTop);
            const Bool selected = state.interaction.IsSelected(source.id);
            const Bool nodeHovered = state.hovered.kind == HitKind::Node && state.hovered.node.Get() == source.id.Get();
            draw.AddRect(topLeft, bottomRight, ToU32(selected ? EditorTheme::Accent : (nodeHovered ? EditorTheme::LineStrong : EditorTheme::Line)),
                rounding, ImDrawFlags_None, selected ? 2.0f : 1.0f);
            if (drawText)
            {
                const ImVec2 titleAt(topLeft.x + 8.0f * zoom, topLeft.y + (style.headerHeight.Get() * zoom - fontSize) * 0.5f);
                draw.AddText(font, fontSize, titleAt, ToU32(type == nullptr ? EditorTheme::Danger : EditorTheme::Ink), source.type.c_str());
            }
            if (type == nullptr)
            {
                continue;
            }

            NodeGraph::PinRef origin;
            Bool originFromOutput = false;
            const Bool linking = state.interaction.GetLinkOrigin(origin, originFromOutput);
            const float pinRadius = style.pinRadius.Get() * zoom;
            for (std::size_t pin = 0; pin < type->inputs.Size(); ++pin)
            {
                const ImVec2 at = ToIm(ViewCamera::ToScreen(state.camera, NodeGraphView::InputPosition(node, pin, style)));
                const NodeGraph::Link* link = graph.FindLinkInto(NodeGraph::PinRef{source.id, pin});
                const ImU32 color = ToU32(PinColor(domain, type->inputs[pin].type));
                if (link != nullptr)
                {
                    draw.AddCircleFilled(at, pinRadius, color);
                }
                else
                {
                    draw.AddCircle(at, pinRadius, color, 0, std::max(1.0f, 1.5f * zoom));
                }
                const Hit pinHit{HitKind::Input, source.id, pin, NodeGraph::InvalidId};
                const Bool pinHovered = state.hovered.kind == HitKind::Input && state.hovered.node.Get() == source.id.Get() && state.hovered.pin == pin;
                if (pinHovered && (!linking || state.interaction.CanLinkTo(graph, domain, pinHit)))
                {
                    draw.AddCircle(at, pinRadius + 3.0f * zoom, ToU32(EditorTheme::AccentBright), 0, 2.0f);
                }
                if (drawText)
                {
                    const ImVec2 labelAt(at.x + pinRadius + 6.0f * zoom, at.y - fontSize * 0.5f);
                    draw.AddText(font, fontSize, labelAt, ToU32(EditorTheme::InkMuted), type->inputs[pin].name.c_str());
                    const NodeGraph::PinTypeInfo* pinType = domain.FindPinType(type->inputs[pin].type);
                    if (link == nullptr && pinType != nullptr && pinType->components.Get() == 1 && pin < source.inputs.Size())
                    {
                        // 이어지지 않은 한 칸 입력은 값을 이름 옆에 보인다. 고치는 것은 옆 칸이다.
                        char value[32];
                        FormatValue(value, sizeof(value), source.inputs[pin]);
                        const float nameWidth = font->CalcTextSizeA(fontSize, 1.0e9f, 0.0f, type->inputs[pin].name.c_str()).x;
                        draw.AddText(font, fontSize, ImVec2(labelAt.x + nameWidth + 6.0f * zoom, labelAt.y), ToU32(EditorTheme::Ink), value);
                    }
                }
            }
            for (std::size_t pin = 0; pin < type->outputs.Size(); ++pin)
            {
                const ImVec2 at = ToIm(ViewCamera::ToScreen(state.camera, NodeGraphView::OutputPosition(node, pin, style)));
                const Bool connected = graph.GetLinks().Size() != 0 && [&]()
                {
                    for (const NodeGraph::Link& link : graph.GetLinks())
                    {
                        if (link.from.node.Get() == source.id.Get() && link.from.pin == pin)
                        {
                            return true;
                        }
                    }
                    return false;
                }();
                const ImU32 color = ToU32(PinColor(domain, type->outputs[pin].type));
                if (connected)
                {
                    draw.AddCircleFilled(at, pinRadius, color);
                }
                else
                {
                    draw.AddCircle(at, pinRadius, color, 0, std::max(1.0f, 1.5f * zoom));
                }
                const Hit pinHit{HitKind::Output, source.id, pin, NodeGraph::InvalidId};
                const Bool pinHovered = state.hovered.kind == HitKind::Output && state.hovered.node.Get() == source.id.Get() && state.hovered.pin == pin;
                if (pinHovered && (!linking || state.interaction.CanLinkTo(graph, domain, pinHit)))
                {
                    draw.AddCircle(at, pinRadius + 3.0f * zoom, ToU32(EditorTheme::AccentBright), 0, 2.0f);
                }
                if (drawText)
                {
                    const char* name = type->outputs[pin].name.c_str();
                    const float width = font->CalcTextSizeA(fontSize, 1.0e9f, 0.0f, name).x;
                    draw.AddText(font, fontSize, ImVec2(at.x - pinRadius - 6.0f * zoom - width, at.y - fontSize * 0.5f), ToU32(EditorTheme::InkMuted), name);
                }
            }
        }

        // ── 잇는 중인 선 ──
        NodeGraph::PinRef origin;
        Bool fromOutput = false;
        if (state.interaction.GetLinkOrigin(origin, fromOutput))
        {
            if (const NodeGraph::Node* node = graph.FindNode(origin.node))
            {
                const Vector2 pinAt = ViewCamera::ToScreen(state.camera, fromOutput
                    ? NodeGraphView::OutputPosition(*node, origin.pin, style)
                    : NodeGraphView::InputPosition(*node, origin.pin, style));
                const NodeGraphView::Curve curve = fromOutput ? NodeGraphView::LinkCurve(pinAt, input.mouse, state.camera.zoom)
                                                              : NodeGraphView::LinkCurve(input.mouse, pinAt, state.camera.zoom);
                DrawCurve(draw, curve, ToU32(EditorTheme::AccentBright), std::max(1.5f, 2.5f * zoom));
            }
        }

        // ── 상자 고르기 ──
        Rect box;
        if (state.interaction.GetBoxRect(box))
        {
            draw.AddRectFilled(ToIm(box.min), ToIm(box.max), ToU32(EditorTheme::Fade(EditorTheme::Accent, 0.12f)));
            draw.AddRect(ToIm(box.min), ToIm(box.max), ToU32(EditorTheme::Accent));
        }
        draw.PopClipRect();
    }
}
