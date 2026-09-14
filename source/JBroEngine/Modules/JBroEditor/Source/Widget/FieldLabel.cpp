#include <JBro/Editor/Widget/FieldLabel.h>

namespace JBro::Widget
{
    namespace
    {
        constexpr ImVec4 DisabledLabelColor(0.55f, 0.55f, 0.55f, 1.0f);
        constexpr ImVec4 InvalidLabelColor(0.95f, 0.35f, 0.30f, 1.0f);
        constexpr ImVec4 RequiredMarkColor(0.95f, 0.35f, 0.30f, 1.0f);
    }

    FieldLabel::FieldLabel(const char* text)
        : m_text(text)
    {
    }

    FieldLabel& FieldLabel::Tooltip(const char* text)
    {
        m_tooltip = text;
        return *this;
    }

    FieldLabel& FieldLabel::Required(bool required)
    {
        m_required = required;
        return *this;
    }

    FieldLabel& FieldLabel::Invalid(bool invalid)
    {
        m_invalid = invalid;
        return *this;
    }

    FieldLabel& FieldLabel::Disabled(bool disabled)
    {
        m_disabled = disabled;
        return *this;
    }

    void FieldLabel::Draw() const
    {
        StyleScope style;
        if (m_disabled)
        {
            style.PushColor(ImGuiCol_Text, DisabledLabelColor);
        }
        else if (m_invalid)
        {
            style.PushColor(ImGuiCol_Text, InvalidLabelColor);
        }
        ImGui::TextUnformatted(m_text != nullptr ? m_text : "");
        style.Pop();

        HoveredTooltip(m_tooltip);

        if (m_required)
        {
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextColored(RequiredMarkColor, "*");
        }
    }

    void FieldLabel::operator()() const
    {
        Draw();
    }

    SectionHeader::SectionHeader(const char* title)
        : m_title(title)
    {
    }

    SectionHeader& SectionHeader::Description(const char* text)
    {
        m_description = text;
        return *this;
    }

    SectionHeader& SectionHeader::SpacingBefore(bool spacing)
    {
        m_spacingBefore = spacing;
        return *this;
    }

    SectionHeader& SectionHeader::SpacingAfter(bool spacing)
    {
        m_spacingAfter = spacing;
        return *this;
    }

    void SectionHeader::Draw() const
    {
        if (m_spacingBefore)
        {
            ImGui::Spacing();
        }
        if (false == IsEmptyText(m_title))
        {
            ImGui::SeparatorText(m_title);
        }
        else
        {
            ImGui::Separator();
        }
        if (false == IsEmptyText(m_description))
        {
            ImGui::TextWrapped("%s", m_description);
        }
        if (m_spacingAfter)
        {
            ImGui::Spacing();
        }
    }

    void SectionHeader::operator()() const
    {
        Draw();
    }

    ValidationMessage::ValidationMessage(Severity severity, const char* text)
        : m_severity(severity)
        , m_text(text)
    {
    }

    ValidationMessage& ValidationMessage::Wrapped(bool wrapped)
    {
        m_wrapped = wrapped;
        return *this;
    }

    void ValidationMessage::Draw() const
    {
        if (IsEmptyText(m_text))
        {
            return;
        }
        StyleScope style;
        style.PushColor(ImGuiCol_Text, SeverityColor(m_severity));
        if (m_wrapped)
        {
            ImGui::TextWrapped("%s%s", SeverityPrefix(m_severity), m_text);
        }
        else
        {
            ImGui::Text("%s%s", SeverityPrefix(m_severity), m_text);
        }
    }

    void ValidationMessage::operator()() const
    {
        Draw();
    }
}
