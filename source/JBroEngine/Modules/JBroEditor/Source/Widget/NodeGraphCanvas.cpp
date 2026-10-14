#include <JBro/Editor/Widget/NodeGraphCanvas.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/ItemLocator.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
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

        ImVec4 CommentColor(const NodeGraph::Comment& comment, float alphaScale)
        {
            return ImVec4(comment.color.R.Get(), comment.color.G.Get(), comment.color.B.Get(), comment.color.A.Get() * alphaScale);
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

        void ReportRect(const char* label, ImVec2 min, ImVec2 max)
        {
            if (ItemLocator::IsWatching())
            {
                ItemLocator::Report(ImGui::GetID(label), ItemLocator::ItemRect{min.x, min.y, max.x, max.y});
            }
        }

        Bool InsideCanvas(ImVec2 min, ImVec2 max, ImVec2 canvasMin, ImVec2 canvasMax)
        {
            return min.x >= canvasMin.x && min.y >= canvasMin.y && max.x <= canvasMax.x && max.y <= canvasMax.y;
        }

        void DrawComments(ImDrawList& draw, const NodeGraph::Graph& graph, const NodeGraphCanvasState& state, const NodeGraphView::Preview& preview,
            ImFont* font, float fontSize, Bool drawText)
        {
            const float zoom = state.camera.zoom.Get();
            for (const NodeGraph::Comment& source : graph.GetComments())
            {
                const NodeGraph::Comment comment = NodeGraphView::PlacedComment(source, preview);
                const ImVec2 topLeft = ToIm(ViewCamera::ToScreen(state.camera, comment.position));
                const ImVec2 bottomRight = ToIm(ViewCamera::ToScreen(state.camera,
                    Vector2{comment.position.x.Get() + comment.size.x.Get(), comment.position.y.Get() + comment.size.y.Get()}));
                const float headerBottom = topLeft.y + state.style.commentHeaderHeight.Get() * zoom;
                char label[40];
                std::snprintf(label, sizeof(label), "##comment%llu", static_cast<unsigned long long>(source.id.Get()));
                ReportRect(label, topLeft, ImVec2(bottomRight.x, headerBottom));
                const float rounding = 4.0f * zoom;
                draw.AddRectFilled(topLeft, bottomRight, ToU32(CommentColor(source, 0.45f)), rounding);
                draw.AddRectFilled(topLeft, ImVec2(bottomRight.x, headerBottom), ToU32(CommentColor(source, 1.0f)), rounding, ImDrawFlags_RoundCornersTop);
                const Bool selected = state.interaction.IsSelected(source.id);
                const Bool hovered = (state.hovered.kind == HitKind::CommentHeader || state.hovered.kind == HitKind::CommentResize)
                    && state.hovered.comment.Get() == source.id.Get();
                draw.AddRect(topLeft, bottomRight, ToU32(selected ? EditorTheme::Accent : (hovered ? EditorTheme::LineStrong : EditorTheme::Line)),
                    rounding, ImDrawFlags_None, selected ? 2.0f : 1.0f);
                // 크기 손잡이는 오른쪽 아래 모서리의 세모다.
                const float handle = state.style.commentHandle.Get();
                draw.AddTriangleFilled(ImVec2(bottomRight.x - handle, bottomRight.y), bottomRight, ImVec2(bottomRight.x, bottomRight.y - handle),
                    ToU32(state.hovered.kind == HitKind::CommentResize && hovered ? EditorTheme::AccentBright : EditorTheme::LineStrong));
                if (drawText && !source.title.empty())
                {
                    const float titleSize = fontSize * 1.1f;
                    draw.AddText(font, titleSize, ImVec2(topLeft.x + 8.0f * zoom, topLeft.y + (headerBottom - topLeft.y - titleSize) * 0.5f),
                        ToU32(EditorTheme::Ink), source.title.c_str());
                }
            }
        }

        void DrawMinimap(ImDrawList& draw, const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, const NodeGraphCanvasState& state,
            ImVec2 canvasSize)
        {
            NodeGraphView::Minimap minimap;
            if (!NodeGraphView::LayoutMinimap(graph, domain, state.style, state.camera, Vector2{canvasSize.x, canvasSize.y}, minimap))
            {
                return;
            }
            const ImVec2 min = ToIm(minimap.screen.min);
            const ImVec2 max = ToIm(minimap.screen.max);
            ReportRect("##minimap", min, max);
            const Bool hovered = state.hovered.kind == HitKind::Minimap;
            draw.AddRectFilled(min, max, ToU32(EditorTheme::Fade(EditorTheme::Panel, 0.92f)), 4.0f);
            draw.PushClipRect(min, max, true);
            for (const NodeGraph::Comment& comment : graph.GetComments())
            {
                const ImVec2 a = ToIm(NodeGraphView::GraphToMinimap(minimap, comment.position));
                const ImVec2 b = ToIm(NodeGraphView::GraphToMinimap(minimap,
                    Vector2{comment.position.x.Get() + comment.size.x.Get(), comment.position.y.Get() + comment.size.y.Get()}));
                draw.AddRectFilled(a, b, ToU32(CommentColor(comment, 0.6f)));
            }
            for (const NodeGraph::Node& node : graph.GetNodes())
            {
                const NodeGraph::NodeTypeInfo* type = domain.FindNodeType(node.type.View());
                const Rect bounds = NodeGraphView::NodeBounds(node, type, state.style);
                const ImVec2 a = ToIm(NodeGraphView::GraphToMinimap(minimap, bounds.min));
                const ImVec2 b = ToIm(NodeGraphView::GraphToMinimap(minimap, bounds.max));
                const Bool selected = state.interaction.IsSelected(node.id);
                draw.AddRectFilled(a, ImVec2(std::max(b.x, a.x + 2.0f), std::max(b.y, a.y + 2.0f)),
                    ToU32(selected ? EditorTheme::Accent : HeaderColor(domain, type)));
            }
            // 지금 보이는 곳이다.
            const Vector2 viewMin = NodeGraphView::ToGraph(state.camera, state.camera.origin);
            const Vector2 viewMax = NodeGraphView::ToGraph(state.camera,
                Vector2{state.camera.origin.x.Get() + canvasSize.x, state.camera.origin.y.Get() + canvasSize.y});
            draw.AddRect(ToIm(NodeGraphView::GraphToMinimap(minimap, viewMin)), ToIm(NodeGraphView::GraphToMinimap(minimap, viewMax)),
                ToU32(EditorTheme::AccentBright), 0.0f, ImDrawFlags_None, 1.5f);
            draw.PopClipRect();
            draw.AddRect(min, max, ToU32(hovered ? EditorTheme::LineStrong : EditorTheme::Line), 4.0f);
        }

        // 노드 안 값 칸이다(D-321). 바꾸면 그 값을 `events` 에 담는다.
        void DrawInputFields(const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state,
            const NodeGraph::Node& source, const NodeGraph::Node& placed, ImDrawList& draw, float fontSize, NodeGraphView::Events& events)
        {
            const float zoom = state.camera.zoom.Get();
            for (std::size_t pin = 0; pin < source.inputs.Size(); ++pin)
            {
                const UInt32 components = NodeGraphView::InputFieldComponents(graph, domain, state.style, state.camera, source, pin);
                if (components.Get() == 0)
                {
                    continue;
                }
                const Rect bounds = NodeGraphView::InputFieldBounds(placed, pin, components, state.style);
                const ImVec2 min = ToIm(ViewCamera::ToScreen(state.camera, bounds.min));
                const ImVec2 max = ToIm(ViewCamera::ToScreen(state.camera, bounds.max));
                // 캔버스 밖으로 나간 칸은 세우지 않는다 - 창의 내용 크기가 늘어 스크롤바가 생긴다.
                if (!InsideCanvas(min, max, state.canvasMin, state.canvasMax))
                {
                    continue;
                }
                char label[48];
                std::snprintf(label, sizeof(label), "##field%llu_%zu", static_cast<unsigned long long>(source.id.Get()), pin);
                ReportRect(label, min, max);
                // 칸에서 핀으로 가는 짧은 선이다 - 어느 핀의 값인지 보인다.
                const ImVec2 pinAt = ToIm(ViewCamera::ToScreen(state.camera, NodeGraphView::InputPosition(placed, pin, state.style)));
                draw.AddLine(ImVec2(max.x, (min.y + max.y) * 0.5f), ImVec2(pinAt.x - state.style.pinRadius.Get() * zoom, pinAt.y),
                    ToU32(EditorTheme::LineStrong), std::max(1.0f, 1.2f * zoom));

                NodeGraph::PinValue value = source.inputs[pin];
                Bool changed = false;
                const float height = max.y - min.y;
                const float each = (max.x - min.x) / static_cast<float>(components.Get());
                ImGui::PushID(label);
                ImGui::PushFont(nullptr, fontSize);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f * zoom, std::max(0.0f, (height - fontSize) * 0.5f)));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * zoom);
                for (UInt32 component = 0; component < components; ++component)
                {
                    ImGui::SetCursorScreenPos(ImVec2(min.x + each * static_cast<float>(component.Get()), min.y));
                    ImGui::SetNextItemWidth(each - 2.0f * zoom);
                    float edited = value.values[component.Get()].Get();
                    ImGui::PushID(static_cast<int>(component.Get()));
                    if (ImGui::DragFloat("##value", &edited, 0.01f, 0.0f, 0.0f, "%.2f"))
                    {
                        value.values[component.Get()] = edited;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::PopStyleVar(2);
                ImGui::PopFont();
                ImGui::PopID();
                if (changed)
                {
                    events.inputEdited = true;
                    events.editedNode = source.id;
                    events.editedPin = pin;
                    events.editedValue = value;
                }
            }
        }

        Bool Matches(const char* text, const String& filter)
        {
            if (filter.empty())
            {
                return true;
            }
            if (text == nullptr)
            {
                return false;
            }
            // 대소문자를 가리지 않는 부분 일치다. 노드 이름은 영어 타입 이름이다.
            const std::size_t length = std::char_traits<char>::length(text);
            for (std::size_t start = 0; start + filter.size() <= length; ++start)
            {
                Bool same = true;
                for (std::size_t index = 0; index < filter.size(); ++index)
                {
                    if (std::tolower(static_cast<unsigned char>(text[start + index])) != std::tolower(static_cast<unsigned char>(filter[index])))
                    {
                        same = false;
                        break;
                    }
                }
                if (same)
                {
                    return true;
                }
            }
            return false;
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

        // 그림 위의 입력 자리다. 세 단추를 다 받아 창이 끌려가지 않게 한다. 위에 서는 값 칸이 먼저 마우스를 받는다(겹침 허용).
        ImGui::SetNextItemAllowOverlap();
        HitArea(id, canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const Bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const Bool active = ImGui::IsItemActive();

        const ImGuiIO& io = ImGui::GetIO();
        NodeGraphView::Input input;
        input.mouse = Vector2{io.MousePos.x, io.MousePos.y};
        input.canvasSize = Vector2{canvasSize.x, canvasSize.y};
        input.hovered = hovered;
        // 값 칸을 끄는 동안의 누름·놓음은 캔버스의 것이 아니다.
        const Bool fieldActive = ImGui::IsAnyItemActive() && !active;
        input.leftPressed = hovered && !fieldActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
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

    void EndNodeGraphCanvas(const NodeGraph::Graph& graph, const NodeGraph::Domain& domain, NodeGraphCanvasState& state,
        NodeGraphView::Events& events)
    {
        const ImVec2 min = state.canvasMin;
        const ImVec2 max = state.canvasMax;
        const ImVec2 canvasSize(max.x - min.x, max.y - min.y);
        // 값 칸을 세우느라 커서를 옮긴다. 끝나면 커서와 창 내용의 끝을 캔버스 바로 뒤로 되돌려 창의 배치를 그대로 둔다.
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImVec2 cursor = window->DC.CursorPos;
        const ImVec2 cursorMax = window->DC.CursorMaxPos;
        const Bool cursorSet = window->DC.IsSetPos;
        const NodeGraphView::Preview preview = state.interaction.GetPreview();
        state.hovered = state.canvasHovered
            ? NodeGraphView::Pick(graph, domain, state.style, state.camera, state.mouse, preview, Vector2{canvasSize.x, canvasSize.y})
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

        // ── 주석(노드 뒤) ──
        DrawComments(draw, graph, state, preview, font, fontSize, drawText);

        // ── 링크와 경유점 ──
        Array<NodeGraphView::Curve> curves;
        Array<Vector2> points;
        for (const NodeGraph::Link& link : graph.GetLinks())
        {
            const NodeGraph::Node* from = graph.FindNode(link.from.node);
            const NodeGraph::Node* to = graph.FindNode(link.to.node);
            const NodeGraph::NodeTypeInfo* fromType = from == nullptr ? nullptr : domain.FindNodeType(from->type.View());
            if (from == nullptr || to == nullptr || fromType == nullptr || link.from.pin >= fromType->outputs.Size())
            {
                continue;
            }
            const Vector2 a = ViewCamera::ToScreen(state.camera, NodeGraphView::OutputPosition(NodeGraphView::PlacedNode(*from, preview), link.from.pin, style));
            const Vector2 b = ViewCamera::ToScreen(state.camera, NodeGraphView::InputPosition(NodeGraphView::PlacedNode(*to, preview), link.to.pin, style));
            NodeGraphView::PlacedWaypoints(link, preview, points);
            for (Vector2& point : points)
            {
                point = ViewCamera::ToScreen(state.camera, point);
            }
            NodeGraphView::LinkPath(a, points.View(), b, state.camera.zoom, curves);
            const Bool lit = state.hovered.kind == HitKind::Link && state.hovered.link.Get() == link.id.Get();
            const ImVec4 color = PinColor(domain, fromType->outputs[link.from.pin].type);
            for (const NodeGraphView::Curve& curve : curves)
            {
                DrawCurve(draw, curve, ToU32(lit ? EditorTheme::AccentBright : color), std::max(1.5f, (lit ? 3.5f : 2.5f) * zoom));
            }
            for (std::size_t index = 0; index < points.Size(); ++index)
            {
                const Bool pointHovered = state.hovered.kind == HitKind::Waypoint && state.hovered.link.Get() == link.id.Get()
                    && state.hovered.waypoint == index;
                const float radius = style.waypointRadius.Get() * std::max(1.0f, zoom);
                draw.AddCircleFilled(ToIm(points[index]), radius + (pointHovered ? 2.0f : 0.0f), ToU32(pointHovered ? EditorTheme::AccentBright : color));
                char label[48];
                std::snprintf(label, sizeof(label), "##waypoint%llu_%zu", static_cast<unsigned long long>(link.id.Get()), index);
                ReportRect(label, ImVec2(points[index].x.Get() - radius, points[index].y.Get() - radius),
                    ImVec2(points[index].x.Get() + radius, points[index].y.Get() + radius));
            }
        }

        // ── 노드 ──
        const float rounding = 6.0f * zoom;
        NodeGraph::PinRef origin;
        Bool originFromOutput = false;
        const Bool linking = state.interaction.GetLinkOrigin(origin, originFromOutput);
        for (const NodeGraph::Node& source : graph.GetNodes())
        {
            const NodeGraph::Node node = NodeGraphView::PlacedNode(source, preview);
            const NodeGraph::NodeTypeInfo* type = domain.FindNodeType(source.type.View());
            const Rect bounds = NodeGraphView::NodeBounds(node, type, style);
            const ImVec2 topLeft = ToIm(ViewCamera::ToScreen(state.camera, bounds.min));
            const ImVec2 bottomRight = ToIm(ViewCamera::ToScreen(state.camera, bounds.max));
            char label[40];
            std::snprintf(label, sizeof(label), "##node%llu", static_cast<unsigned long long>(source.id.Get()));
            ReportRect(label, topLeft, bottomRight);
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
                Hit pinHit;
                pinHit.kind = HitKind::Input;
                pinHit.node = source.id;
                pinHit.pin = pin;
                const Bool pinHovered = state.hovered.kind == HitKind::Input && state.hovered.node.Get() == source.id.Get() && state.hovered.pin == pin;
                if (pinHovered && (!linking || state.interaction.CanLinkTo(graph, domain, pinHit)))
                {
                    draw.AddCircle(at, pinRadius + 3.0f * zoom, ToU32(EditorTheme::AccentBright), 0, 2.0f);
                }
                if (drawText)
                {
                    const ImVec2 labelAt(at.x + pinRadius + 6.0f * zoom, at.y - fontSize * 0.5f);
                    draw.AddText(font, fontSize, labelAt, ToU32(EditorTheme::InkMuted), type->inputs[pin].name.c_str());
                }
            }
            for (std::size_t pin = 0; pin < type->outputs.Size(); ++pin)
            {
                const ImVec2 at = ToIm(ViewCamera::ToScreen(state.camera, NodeGraphView::OutputPosition(node, pin, style)));
                Bool connected = false;
                for (const NodeGraph::Link& link : graph.GetLinks())
                {
                    if (link.from.node.Get() == source.id.Get() && link.from.pin == pin)
                    {
                        connected = true;
                        break;
                    }
                }
                const ImU32 color = ToU32(PinColor(domain, type->outputs[pin].type));
                if (connected)
                {
                    draw.AddCircleFilled(at, pinRadius, color);
                }
                else
                {
                    draw.AddCircle(at, pinRadius, color, 0, std::max(1.0f, 1.5f * zoom));
                }
                Hit pinHit;
                pinHit.kind = HitKind::Output;
                pinHit.node = source.id;
                pinHit.pin = pin;
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
            // 이어지지 않은 입력의 값 칸이다. 노드를 끄는 동안에는 세우지 않는다 - 끄는 손에 칸이 걸린다.
            if (state.interaction.GetGesture() != NodeGraphView::Gesture::MovingNodes)
            {
                DrawInputFields(graph, domain, state, source, node, draw, ImGui::GetStyle().FontSizeBase * zoom, events);
            }
        }

        // ── 잇는 중인 선 ──
        NodeGraph::PinRef linkOrigin;
        Bool fromOutput = false;
        if (state.interaction.GetLinkOrigin(linkOrigin, fromOutput))
        {
            if (const NodeGraph::Node* node = graph.FindNode(linkOrigin.node))
            {
                const Vector2 pinAt = ViewCamera::ToScreen(state.camera, fromOutput
                    ? NodeGraphView::OutputPosition(*node, linkOrigin.pin, style)
                    : NodeGraphView::InputPosition(*node, linkOrigin.pin, style));
                const NodeGraphView::Curve curve = fromOutput ? NodeGraphView::LinkCurve(pinAt, state.mouse, state.camera.zoom)
                                                              : NodeGraphView::LinkCurve(state.mouse, pinAt, state.camera.zoom);
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

        // ── 미니맵(맨 위) ──
        DrawMinimap(draw, graph, domain, state, canvasSize);
        draw.PopClipRect();
        // `SetCursorScreenPos` 로 되돌리면 ImGui 가 창 경계를 커서로 늘리려는 것으로 본다(단언). 항목을 하나 더 세우면 그 틈만큼 창이 길어져
        // 스크롤바가 선다. 그래서 칸을 세우기 전의 커서와 내용 끝을 그대로 되돌린다.
        window->DC.CursorPos = cursor;
        window->DC.CursorMaxPos = cursorMax;
        window->DC.IsSetPos = cursorSet;
    }

    Int32 NodeSearchMenu(String& filter, Int32& highlight, ArrayView<const NodeMenuItem> items, const char* disabledTooltip)
    {
        if (ImGui::IsWindowAppearing())
        {
            filter.clear();
            highlight = 0;
            ImGui::SetKeyboardFocusHere();
        }
        const String before = filter;
        SearchBox("##nodeSearch", filter).Hint(Loc::TextOr(LocKeys::CommonSearch, "Search")).Width(220.0f).Draw();
        if (filter != before)
        {
            highlight = 0;
        }
        // Enter 는 글자 칸을 끝낸다. 그 프레임에 Enter 가 눌렸으면 밝힌 항목을 고른다.
        const Bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        Int32 chosen = -1;

        if (filter.empty())
        {
            // 묶음마다 하위 메뉴다. 묶음 이름은 노드 종류 이름처럼 번역하지 않는다. 묶음이 없는 것은 맨 위에 둔다.
            ImGui::Separator();
            for (std::size_t index = 0; index < items.Size(); ++index)
            {
                const NodeMenuItem& item = items[index];
                if (item.category != nullptr && item.category[0] != '\0')
                {
                    continue;
                }
                if (MenuItem(item.name, nullptr, item.enabled, disabledTooltip))
                {
                    chosen = static_cast<Int32>(index);
                }
            }
            for (std::size_t index = 0; index < items.Size(); ++index)
            {
                const char* category = items[index].category;
                if (category == nullptr || category[0] == '\0')
                {
                    continue;
                }
                Bool seen = false;
                for (std::size_t earlier = 0; earlier < index; ++earlier)
                {
                    if (items[earlier].category != nullptr && std::char_traits<char>::compare(items[earlier].category, category,
                        std::char_traits<char>::length(category) + 1) == 0)
                    {
                        seen = true;
                        break;
                    }
                }
                if (seen || !BeginMenu(category))
                {
                    continue;
                }
                for (std::size_t inner = index; inner < items.Size(); ++inner)
                {
                    const NodeMenuItem& item = items[inner];
                    if (item.category != nullptr && std::char_traits<char>::compare(item.category, category, std::char_traits<char>::length(category) + 1) == 0
                        && MenuItem(item.name, nullptr, item.enabled, disabledTooltip))
                    {
                        chosen = static_cast<Int32>(inner);
                    }
                }
                EndMenu();
            }
            return chosen;
        }

        // 글자가 있으면 걸러진 한 줄 목록이다. 밝힌 줄은 위·아래 화살표로 옮긴다.
        Array<Int32> visible;
        for (std::size_t index = 0; index < items.Size(); ++index)
        {
            if (Matches(items[index].name, filter) || Matches(items[index].category, filter))
            {
                visible.Add(static_cast<Int32>(index));
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            highlight = std::min(highlight.Get() + 1, static_cast<std::int32_t>(visible.Size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            highlight = std::max(highlight.Get() - 1, 0);
        }
        ImGui::Separator();
        if (visible.IsEmpty())
        {
            ImGui::TextDisabled("%s", Loc::TextOr(LocKeys::CommonNoMatches, "No matches"));
            return -1;
        }
        highlight = std::clamp(highlight.Get(), 0, static_cast<std::int32_t>(visible.Size()) - 1);
        for (std::size_t row = 0; row < visible.Size(); ++row)
        {
            const NodeMenuItem& item = items[static_cast<std::size_t>(visible[row].Get())];
            const Bool lit = static_cast<std::int32_t>(row) == highlight.Get();
            ImGui::PushID(static_cast<int>(row));
            ImGui::BeginDisabled(!item.enabled);
            if (ImGui::Selectable(item.name, lit) && item.enabled)
            {
                chosen = visible[row];
            }
            ImGui::EndDisabled();
            if (!item.enabled)
            {
                HoveredTooltip(disabledTooltip, ImGuiHoveredFlags_AllowWhenDisabled);
            }
            if (item.category != nullptr && item.category[0] != '\0')
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", item.category);
            }
            ImGui::PopID();
        }
        if (chosen < 0 && enter)
        {
            const NodeMenuItem& item = items[static_cast<std::size_t>(visible[static_cast<std::size_t>(highlight.Get())].Get())];
            if (item.enabled)
            {
                chosen = visible[static_cast<std::size_t>(highlight.Get())];
            }
        }
        if (chosen >= 0)
        {
            ImGui::CloseCurrentPopup();
        }
        return chosen;
    }
}
