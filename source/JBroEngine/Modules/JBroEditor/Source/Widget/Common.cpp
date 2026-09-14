#include <JBro/Editor/Widget/Common.h>

#include <algorithm>

namespace JBro::Widget
{
    namespace
    {
        constexpr ImVec4 InvalidBorderColor(0.95f, 0.35f, 0.30f, 1.0f);
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
