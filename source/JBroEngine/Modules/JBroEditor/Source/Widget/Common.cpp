#include <JBro/Editor/Widget/Common.h>

// 세로 구분선은 공개 헤더에 없다. 그리는 규칙은 ImGui 의 것을 그대로 쓴다.
#include <imgui_internal.h>

#include <algorithm>

namespace JBro::Widget
{
    namespace
    {
        constexpr ImVec4 InvalidBorderColor(0.95f, 0.35f, 0.30f, 1.0f);
        // 도구 줄 구분선의 앞뒤 간격. 단추 사이 기본 간격보다 넓어야 무리가 갈린 것으로 읽힌다.
        constexpr float ToolBarSeparatorSpacing = 12.0f;
    }

    bool IsEmptyText(const char* text)
    {
        return text == nullptr || text[0] == '\0';
    }

    ImVec4 SeverityColor(Severity severity)
    {
        switch (severity)
        {
        case Severity::Success:
            return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
        case Severity::Warning:
            return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
        case Severity::Error:
            return ImVec4(0.95f, 0.35f, 0.30f, 1.0f);
        case Severity::Info:
        default:
            return ImVec4(0.65f, 0.75f, 0.95f, 1.0f);
        }
    }

    const char* SeverityPrefix(Severity severity)
    {
        switch (severity)
        {
        case Severity::Success:
            return "[OK] ";
        case Severity::Warning:
            return "[!] ";
        case Severity::Error:
            return "[X] ";
        case Severity::Info:
        default:
            return "[i] ";
        }
    }

    ImVec4 WithAlpha(ImVec4 color, float alpha)
    {
        color.w = alpha;
        return color;
    }

    ImVec4 ScaleColor(ImVec4 color, float scale)
    {
        color.x = std::clamp(color.x * scale, 0.0f, 1.0f);
        color.y = std::clamp(color.y * scale, 0.0f, 1.0f);
        color.z = std::clamp(color.z * scale, 0.0f, 1.0f);
        return color;
    }

    void HoveredTooltip(const char* text, ImGuiHoveredFlags flags)
    {
        if (IsEmptyText(text))
        {
            return;
        }
        if (ImGui::IsItemHovered(flags))
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    bool MouseWasDragged(ImGuiMouseButton button)
    {
        // `GetMouseDragDelta` 는 누른 자리에서의 거리이고 **놓는 프레임까지 살아 있다**.
        // `IsMouseDragging` 은 누르고 있는 동안만 참이라 뗄 때 묻는 자리에는 맞지 않는다.
        const ImVec2 delta = ImGui::GetMouseDragDelta(button, 0.0f);
        const float threshold = ImGui::GetIO().MouseDragThreshold;
        return (delta.x * delta.x + delta.y * delta.y) > (threshold * threshold);
    }

    void ToolBarSeparator()
    {
        ImGui::SameLine(0.0f, ToolBarSeparatorSpacing);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, ToolBarSeparatorSpacing);
    }

    void StyleScope::Pop()
    {
        if (m_vars > 0)
        {
            ImGui::PopStyleVar(m_vars);
            m_vars = 0;
        }
        if (m_colors > 0)
        {
            ImGui::PopStyleColor(m_colors);
            m_colors = 0;
        }
    }

    StyleScope::~StyleScope()
    {
        Pop();
    }

    DisableScope::DisableScope(bool disable)
        : m_disabled(disable)
    {
        if (m_disabled)
        {
            ImGui::BeginDisabled();
        }
    }

    DisableScope::~DisableScope()
    {
        if (m_disabled)
        {
            ImGui::EndDisabled();
        }
    }

    bool DisableScope::IsDisabled() const
    {
        return m_disabled;
    }

    InvalidScope::InvalidScope(bool invalid)
        : m_invalid(invalid)
    {
        if (m_invalid)
        {
            ImGui::PushStyleColor(ImGuiCol_Border, InvalidBorderColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
        }
    }

    InvalidScope::~InvalidScope()
    {
        if (m_invalid)
        {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
    }

    IdScope::IdScope(const char* id)
    {
        ImGui::PushID(id);
    }

    IdScope::IdScope(int id)
    {
        ImGui::PushID(id);
    }

    IdScope::IdScope(const void* id)
    {
        ImGui::PushID(id);
    }

    IdScope::~IdScope()
    {
        ImGui::PopID();
    }
}
