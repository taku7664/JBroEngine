#include <JBro/Editor/Widget/Gizmo.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/GuideFocus.h>

#include <imgui_internal.h>

#include <algorithm>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

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

        ImU32 AxisColor(GizmoAxis axis, Bool highlighted)
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

        // **손잡이를 가이드 포커스에 알린다**(반례 ⑦). 자리는 집기(`GizmoModel::Pick`)가 쓰는 모양 그대로다 - 축은 선분을 집는 거리만큼
        // 부풀린 사각형, 가운데와 회전 고리는 둥근 구멍이다. 눌림은 그 손잡이로 끌기를 시작한 프레임이다.
        void ReportHandles(GizmoMode mode, const GizmoHandleShape* handles, UInt32 count, const GizmoOutput& output)
        {
            const EditorGuideFocus* focus = GetGuideFocus();
            if (focus == nullptr || false == focus->IsActive())
            {
                return;
            }
            for (UInt32 index = 0; index < count; ++index)
            {
                const GizmoHandleShape& handle = handles[index];
                const GuideFocusTarget target = GuideFocusTargets::GizmoHandle(static_cast<std::uint32_t>(mode),
                    static_cast<std::uint32_t>(handle.axis));
                ImVec2 min;
                ImVec2 max;
                Bool round = true;
                if (handle.ring)
                {
                    min = ImVec2(handle.ringX[0], handle.ringY[0]);
                    max = min;
                    for (UInt32 point = 1; point < GizmoHandleShape::RingPoints; ++point)
                    {
                        min = ImVec2(JBro::Min(min.x, handle.ringX[point]), JBro::Min(min.y, handle.ringY[point]));
                        max = ImVec2(JBro::Max(max.x, handle.ringX[point]), JBro::Max(max.y, handle.ringY[point]));
                    }
                }
                else if (handle.axis == GizmoAxis::Free)
                {
                    const Float radius = GizmoModel::CenterRadiusPixels + 3.0f;
                    min = ImVec2(handle.x0 - radius, handle.y0 - radius);
                    max = ImVec2(handle.x0 + radius, handle.y0 + radius);
                }
                else
                {
                    const Float pad = GizmoModel::PickDistancePixels;
                    min = ImVec2((std::min)(handle.x0, handle.x1) - pad, (std::min)(handle.y0, handle.y1) - pad);
                    max = ImVec2((std::max)(handle.x0, handle.x1) + pad, (std::max)(handle.y0, handle.y1) + pad);
                    round = false;
                }
                const Bool activated = output.dragStarted && output.axis == handle.axis;
                ReportGuideTarget(target, min, max, false, activated, round);
            }
        }

        void DrawHandle(ImDrawList& draw, GizmoMode mode, const GizmoHandleShape& handle, Bool highlighted)
        {
            const ImU32 color = AxisColor(handle.axis, highlighted);
            const Float thickness = highlighted ? 3.0f : 2.0f;
            if (handle.ring)
            {
                for (UInt32 index = 0; index < GizmoHandleShape::RingPoints; ++index)
                {
                    const UInt32 next = (index + 1) % GizmoHandleShape::RingPoints;
                    draw.AddLine(ImVec2(handle.ringX[index], handle.ringY[index]),
                        ImVec2(handle.ringX[next], handle.ringY[next]), color, thickness);
                }
                return;
            }
            if (handle.axis == GizmoAxis::Free)
            {
                const Float radius = GizmoModel::CenterRadiusPixels;
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
            const Float dx = to.x - from.x;
            const Float dy = to.y - from.y;
            const Float length = ImSqrt(dx * dx + dy * dy);
            if (length < 1.0f)
            {
                return;
            }
            const Float ux = dx / length;
            const Float uy = dy / length;
            if (mode == GizmoMode::Scale)
            {
                constexpr Float half = 5.0f;
                draw.AddRectFilled(ImVec2(to.x - half, to.y - half), ImVec2(to.x + half, to.y + half), color);
            }
            else
            {
                constexpr Float size = 10.0f;
                const ImVec2 base(to.x - ux * size, to.y - uy * size);
                const ImVec2 side(-uy * size * 0.5f, ux * size * 0.5f);
                draw.AddTriangleFilled(to, ImVec2(base.x + side.x, base.y + side.y),
                    ImVec2(base.x - side.x, base.y - side.y), color);
            }
        }
    }

    GizmoOutput Gizmo(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
        GizmoState& state, Bool interactive, Float snapStep, Float snapRadians)
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
        const Bool mouseInside = mouse.x >= camera.left && mouse.x < camera.left + camera.width
            && mouse.y >= camera.top && mouse.y < camera.top + camera.height;

        if (state.dragging)
        {
            // 끌던 것이 끝났거나 이어진다. 놓는 프레임까지 마지막 마우스로 결과를 낸다.
            if (GizmoModel::UpdateDrag(state.drag, camera, mouse.x, mouse.y, output.subject))
            {
                GizmoModel::SnapTranslation(state.drag, snapStep, output.subject);
                GizmoModel::SnapRotation(state.drag, snapRadians, output.subject);
            }
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
            const Bool canHover = interactive && mouseInside
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
        const UInt32 count = GizmoModel::BuildHandles(mode, camera, output.subject, handles);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const GizmoAxis highlighted = state.dragging ? state.drag.axis : state.hovered;
        draw->PushClipRect(ImVec2(camera.left, camera.top),
            ImVec2(camera.left + camera.width, camera.top + camera.height), true);
        for (UInt32 index = 0; index < count; ++index)
        {
            DrawHandle(*draw, mode, handles[index], handles[index].axis == highlighted);
        }
        draw->PopClipRect();
        ReportHandles(mode, handles, count, output);
        return output;
    }

    void OverlayHandle(const char* id, Bool hovered, Bool holding, Bool pressed)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window == nullptr)
        {
            return;
        }
        const ImGuiID item = window->GetID(id);
        if (holding)
        {
            ImGui::SetActiveID(item, window);
            ImGui::KeepAliveID(item);
            if (pressed)
            {
                // hover id 가 있으면 ImGui 가 창에 포커스를 주지 않는다. 손잡이를 잡는 것도 클릭이다.
                ImGui::FocusWindow(window);
            }
            return;
        }
        if (ImGui::GetActiveID() == item)
        {
            ImGui::ClearActiveID();
        }
        if (hovered)
        {
            ImGui::SetHoveredID(item);
        }
    }

    Bool GizmoModeBar(GizmoMode& mode, const char* translateLabel, const char* rotateLabel, const char* scaleLabel)
    {
        const GizmoMode before = mode;
        const char* labels[3] = {translateLabel, rotateLabel, scaleLabel};
        const char* icons[3] = {Icons::Move, Icons::Rotate, Icons::Scale};
        // 단추의 Id 다. 글자가 없으니 번역이 바뀌어도 같은 Id 다.
        const char* ids[3] = {"##gizmo_translate", "##gizmo_rotate", "##gizmo_scale"};
        const GizmoMode modes[3] = {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale};
        for (Int32 index = 0; index < 3; ++index)
        {
            if (index != 0)
            {
                ImGui::SameLine();
            }
            const Bool selected = mode == modes[index];
            const Bool pressed = IconButton(ids[index], icons[index]).Selected(selected).Tooltip(labels[index]).Draw();
            // 가이드가 모드 단추를 가리킬 수 있다(반례 ⑦). 열림은 그 모드가 켜져 있는가다 - 켜져 있으면 다음 칸(손잡이)으로 간다.
            Internal::ReportLastItem(GuideFocusTargets::GizmoModeButton(static_cast<std::uint32_t>(modes[index])),
                pressed || mode == modes[index], pressed);
            if (pressed)
            {
                mode = modes[index];
            }
        }
        return mode != before;
    }
}
