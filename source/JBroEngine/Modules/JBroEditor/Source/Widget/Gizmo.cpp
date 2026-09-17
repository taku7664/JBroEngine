#include <JBro/Editor/Widget/Gizmo.h>

#include <imgui_internal.h>

namespace JBro::Widget
{
    namespace
    {
        // 손잡이마다 Id 가 있다. 마우스 아래의 손잡이가 ImGui 의 hovered Id 로 보이므로 테스트가 화면을 훑어
        // 손잡이를 찾을 수 있고, 끄는 동안은 그 Id 가 활성이라 창이 함께 끌리지 않는다.
        const char* AxisIdName(GizmoAxis axis)
        {
            switch (axis)
            {
            case GizmoAxis::X:
                return "##gizmo_x";
            case GizmoAxis::Y:
                return "##gizmo_y";
            case GizmoAxis::Z:
                return "##gizmo_z";
            case GizmoAxis::Free:
                return "##gizmo_free";
            default:
                return "##gizmo";
            }
        }

        ImU32 AxisColor(GizmoAxis axis, bool highlighted)
        {
            if (highlighted)
            {
                return IM_COL32(255, 220, 60, 255);
            }
            switch (axis)
            {
            case GizmoAxis::X:
                return IM_COL32(230, 70, 70, 255);
            case GizmoAxis::Y:
                return IM_COL32(90, 210, 90, 255);
            case GizmoAxis::Z:
                return IM_COL32(80, 130, 255, 255);
            default:
                return IM_COL32(240, 240, 240, 255);
            }
        }

        void DrawHandle(ImDrawList& draw, GizmoMode mode, const GizmoHandleShape& handle, bool highlighted)
        {
            const ImU32 color = AxisColor(handle.axis, highlighted);
            const float thickness = highlighted ? 3.0f : 2.0f;
            if (handle.ring)
            {
                for (std::uint32_t index = 0; index < GizmoHandleShape::RingPoints; ++index)
                {
                    const std::uint32_t next = (index + 1) % GizmoHandleShape::RingPoints;
                    draw.AddLine(ImVec2(handle.ringX[index], handle.ringY[index]),
                        ImVec2(handle.ringX[next], handle.ringY[next]), color, thickness);
                }
                return;
            }
            if (handle.axis == GizmoAxis::Free)
            {
                const float radius = GizmoModel::CenterRadiusPixels;
                const ImVec2 center(handle.x0, handle.y0);
                if (mode == GizmoMode::Scale)
                {
                    draw.AddCircle(center, radius, color, 16, thickness);
                }
                else
                {
                    draw.AddRect(ImVec2(center.x - radius, center.y - radius),
                        ImVec2(center.x + radius, center.y + radius), color, 0.0f, 0, thickness);
                }
                return;
            }
            const ImVec2 from(handle.x0, handle.y0);
            const ImVec2 to(handle.x1, handle.y1);
            draw.AddLine(from, to, color, thickness);
            // 끝: 이동은 화살촉, 크기는 상자.
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = ImSqrt(dx * dx + dy * dy);
            if (length < 1.0f)
            {
                return;
            }
            const float ux = dx / length;
            const float uy = dy / length;
            if (mode == GizmoMode::Scale)
            {
                constexpr float half = 5.0f;
                draw.AddRectFilled(ImVec2(to.x - half, to.y - half), ImVec2(to.x + half, to.y + half), color);
            }
            else
            {
                constexpr float size = 10.0f;
                const ImVec2 base(to.x - ux * size, to.y - uy * size);
                const ImVec2 side(-uy * size * 0.5f, ux * size * 0.5f);
                draw.AddTriangleFilled(to, ImVec2(base.x + side.x, base.y + side.y),
                    ImVec2(base.x - side.x, base.y - side.y), color);
            }
        }
    }

    GizmoOutput Gizmo(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
        GizmoState& state, bool interactive)
    {
        GizmoOutput output;
        output.subject = subject;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window == nullptr || camera.width <= 0.0f || camera.height <= 0.0f)
        {
            state.dragging = false;
            state.hovered = GizmoAxis::None;
            return output;
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool mouseInside = mouse.x >= camera.left && mouse.x < camera.left + camera.width
            && mouse.y >= camera.top && mouse.y < camera.top + camera.height;

        if (state.dragging)
        {
            // 끌던 것이 끝났거나 이어진다. 놓는 프레임까지 마지막 마우스로 결과를 낸다.
            GizmoModel::UpdateDrag(state.drag, camera, mouse.x, mouse.y, output.subject);
            output.axis = state.drag.axis;
            output.dragging = true;
            const ImGuiID id = window->GetID(AxisIdName(state.drag.axis));
            if (false == ImGui::IsMouseDown(ImGuiMouseButton_Left) || false == interactive)
            {
                state.dragging = false;
                output.dragEnded = true;
                if (ImGui::GetActiveID() == id)
                {
                    ImGui::ClearActiveID();
                }
            }
            else
            {
                // 끄는 동안은 이 위젯이 활성이다 - 창 이동이나 다른 위젯이 같은 마우스를 받지 않는다.
                ImGui::SetActiveID(id, window);
                ImGui::KeepAliveID(id);
            }
        }
        else
        {
            const bool canHover = interactive && mouseInside
                && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            state.hovered = canHover ? GizmoModel::Pick(mode, camera, subject, mouse.x, mouse.y) : GizmoAxis::None;
            output.hovered = state.hovered != GizmoAxis::None;
            output.axis = state.hovered;
            if (output.hovered)
            {
                const ImGuiID id = window->GetID(AxisIdName(state.hovered));
                ImGui::SetHoveredID(id);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                    && GizmoModel::BeginDrag(mode, state.hovered, camera, subject, mouse.x, mouse.y, state.drag))
                {
                    state.dragging = true;
                    output.dragStarted = true;
                    output.dragging = true;
                    ImGui::SetActiveID(id, window);
                    ImGui::KeepAliveID(id);
                    // 손잡이를 잡는 것도 클릭이다. hovered Id 가 있으면 ImGui 가 창에 포커스를 주지 않으므로 직접 준다 -
                    // 그래야 이어지는 W·E·R 이 이 창의 것이 된다.
                    ImGui::FocusWindow(window);
                }
            }
        }

        // 그리기는 끌기 중이면 결과 위치에, 아니면 대상 위치에.
        GizmoHandleShape handles[GizmoModel::MaxHandles];
        const std::uint32_t count = GizmoModel::BuildHandles(mode, camera, output.subject, handles);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const GizmoAxis highlighted = state.dragging ? state.drag.axis : state.hovered;
        draw->PushClipRect(ImVec2(camera.left, camera.top),
            ImVec2(camera.left + camera.width, camera.top + camera.height), true);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            DrawHandle(*draw, mode, handles[index], handles[index].axis == highlighted);
        }
        draw->PopClipRect();
        return output;
    }

    bool GizmoModeBar(GizmoMode& mode, const char* translateLabel, const char* rotateLabel, const char* scaleLabel,
        bool hotkeys)
    {
        const GizmoMode before = mode;
        const char* labels[3] = {translateLabel, rotateLabel, scaleLabel};
        // 라벨 뒤에 붙는 안정된 꼬리다. Id 는 `라벨##꼬리` 에서 나오므로 번역이 바뀌어도 꼬리로 찾을 수 있다.
        const char* suffixes[3] = {"##gizmo_translate", "##gizmo_rotate", "##gizmo_scale"};
        const GizmoMode modes[3] = {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale};
        for (int index = 0; index < 3; ++index)
        {
            if (index != 0)
            {
                ImGui::SameLine();
            }
            const bool selected = mode == modes[index];
            StyleScope style;
            if (selected)
            {
                style.PushColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            char label[128];
            ImFormatString(label, sizeof(label), "%s%s", labels[index] != nullptr ? labels[index] : "", suffixes[index]);
            if (ImGui::Button(label))
            {
                mode = modes[index];
            }
        }
        // 포커스가 있거나 마우스가 이 창 위에 있으면 받는다. 글자 입력 중이거나 조합키(Ctrl+S 같은 단축키)가 눌려 있으면
        // 받지 않는다.
        const ImGuiIO& io = ImGui::GetIO();
        if (hotkeys && false == io.WantTextInput && false == io.KeyCtrl && false == io.KeyAlt && false == io.KeySuper
            && (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)
                || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            {
                mode = GizmoMode::Translate;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            {
                mode = GizmoMode::Rotate;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            {
                mode = GizmoMode::Scale;
            }
        }
        return mode != before;
    }
}
