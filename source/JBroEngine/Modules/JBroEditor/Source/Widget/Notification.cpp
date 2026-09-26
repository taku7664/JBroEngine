#include <JBro/Editor/Widget/Notification.h>

#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Widget/Button.h>

#include <imgui_internal.h>

#include <cstdio>

namespace JBro::Widget
{
    namespace
    {
        Severity ToSeverity(NotificationLevel level)
        {
            switch (level)
            {
            case NotificationLevel::Success:
                return Severity::Success;
            case NotificationLevel::Warning:
                return Severity::Warning;
            case NotificationLevel::Error:
                return Severity::Error;
            case NotificationLevel::Info:
            default:
                return Severity::Info;
            }
        }

        // 제목 줄 오른쪽에 닫기 표시와 횟수가 설 자리.
        float TitleReserve()
        {
            return ImGui::GetFontSize() * 3.0f;
        }

        float MessageWrapWidth(const NotificationStackStyle& style)
        {
            return style.width - style.accentWidth - style.padding * 2.0f;
        }

        // 제목은 닫기 표시와 횟수 자리를 비켜 줄을 바꾼다.
        float TitleWrapWidth(const NotificationStackStyle& style)
        {
            return MessageWrapWidth(style) - TitleReserve();
        }

        float TitleHeight(const NotificationView& view, const NotificationStackStyle& style)
        {
            return ImGui::CalcTextSize(view.title, nullptr, false, TitleWrapWidth(style)).y;
        }
    }

    float NotificationHeight(const NotificationView& view, const NotificationStackStyle& style)
    {
        float height = style.padding * 2.0f + TitleHeight(view, style);
        if (false == IsEmptyText(view.message))
        {
            const ImVec2 size = ImGui::CalcTextSize(view.message, nullptr, false, MessageWrapWidth(style));
            height += ImGui::GetStyle().ItemSpacing.y + size.y;
        }
        return height;
    }

    NotificationHandle NotificationStack(EditorNotifications& notifications, const NotificationStackStyle& style)
    {
        NotificationHandle activated = InvalidNotificationHandle;
        const std::uint32_t count = notifications.GetVisibleCount();
        if (count == 0)
        {
            return activated;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float right = viewport->WorkPos.x + viewport->WorkSize.x - style.margin;
        const float bottom = viewport->WorkPos.y + viewport->WorkSize.y - style.margin - style.bottomInset;

        // **가장 새 것부터 바닥에 쌓는다.** 사라지는 것은 남은 몫만큼만 높이를 차지해, 위의 것들이
        // 뚝 떨어지지 않고 내려온다. 들어오는 것은 처음부터 제 높이를 차지한다 - 그래야 새 알림이 올 때
        // 앞의 것들이 위로 올라간다.
        float stacked = 0.0f;
        for (std::uint32_t reversed = 0; reversed < count; ++reversed)
        {
            const std::uint32_t index = count - 1 - reversed;
            const NotificationView measured = notifications.GetVisible(index);
            const float height = NotificationHeight(measured, style);
            notifications.ReportLayout(measured.handle, style.width, stacked);
            stacked += (height + style.spacing) * measured.space;

            const NotificationView view = notifications.GetVisible(index);
            const ImVec2 position(right - style.width + view.offsetX, bottom - view.offsetY - height);
            const ImVec4 accent = SeverityColor(ToSeverity(view.level));

            char name[48] = {};
            std::snprintf(name, sizeof(name), "##notification_%llu", static_cast<unsigned long long>(view.handle));
            ImGui::SetNextWindowPos(position, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(style.width, height), ImGuiCond_Always);
            StyleScope scope;
            scope.PushVar(ImGuiStyleVar_Alpha, view.alpha);
            scope.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            scope.PushVar(ImGuiStyleVar_WindowRounding, 6.0f);
            scope.PushVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            scope.PushColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse;
            const bool open = ImGui::Begin(name, nullptr, flags);
            if (open)
            {
                // **모든 창 위에 선다.** 도크의 창을 누르면 그 창이 앞으로 오는데, 알림이 그 뒤로 숨으면
                // 알림이 아니다. 매 프레임 맨 앞으로 올린다.
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 origin = ImGui::GetWindowPos();
                drawList->AddRectFilled(origin, ImVec2(origin.x + style.accentWidth, origin.y + height),
                    ImGui::GetColorU32(accent), 6.0f, ImDrawFlags_RoundCornersLeft);

                bool hovered = false;
                if (false == view.leaving)
                {
                    // 상자 전체가 손짓을 받는다. 닫기 표시가 그 위에 겹쳐 설 수 있게 겹침을 연다.
                    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::InvisibleButton("##body", ImVec2(style.width, height));
                    const bool active = ImGui::IsItemActive();
                    // **상자 창 단위로 잰다.** 본문 단추로 재면 닫기 표시를 누르는 동안 본문이 올려진 것이 아니게
                    // 되고, 그러면 닫기 표시가 사라져 떼는 프레임에 눌림이 서지 않는다.
                    hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || active;
                    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    {
                        notifications.Drag(view.handle, ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x);
                    }
                    if (ImGui::IsItemDeactivated())
                    {
                        if (MouseWasDragged(ImGuiMouseButton_Left))
                        {
                            notifications.Release(view.handle);
                        }
                        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
                        {
                            activated = view.handle;
                        }
                    }
                    if (hovered && view.hasAction)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    notifications.SetHovered(view.handle, hovered);
                }

                const float left = style.accentWidth + style.padding;
                ImGui::SetCursorPos(ImVec2(left, style.padding));
                ImGui::PushTextWrapPos(left + TitleWrapWidth(style));
                ImGui::TextColored(accent, "%s", view.title);
                ImGui::PopTextWrapPos();
                if (view.count > 1)
                {
                    ImGui::SetCursorPos(ImVec2(style.width - style.padding - TitleReserve(), style.padding));
                    ImGui::TextDisabled("x%u", view.count);
                }
                if (hovered)
                {
                    const float closeSize = ImGui::GetTextLineHeight();
                    ImGui::SetCursorPos(ImVec2(style.width - style.padding - closeSize, style.padding));
                    if (TextButton(Icons::Xmark, ImVec2(closeSize, closeSize)))
                    {
                        notifications.Dismiss(view.handle);
                    }
                }
                if (false == IsEmptyText(view.message))
                {
                    ImGui::SetCursorPos(ImVec2(left, style.padding + TitleHeight(view, style) + ImGui::GetStyle().ItemSpacing.y));
                    ImGui::PushTextWrapPos(left + MessageWrapWidth(style));
                    ImGui::TextUnformatted(view.message);
                    ImGui::PopTextWrapPos();
                }
            }
            ImGui::End();
        }
        return activated;
    }
}
