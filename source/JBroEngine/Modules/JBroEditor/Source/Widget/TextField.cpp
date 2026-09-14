#include <JBro/Editor/Widget/TextField.h>

#include <cstring>

namespace JBro::Widget
{
    namespace
    {
        // 한 프레임 동안만 쓰는 버퍼다. 글자 칸은 한 번에 하나만 활성이므로
        // 하나면 된다 - 여러 칸을 같은 프레임에 그려도 각자 그릴 때만 쓴다.
        constexpr std::size_t FieldCapacity = 1024;
    }

    TextField::TextField(const char* id, String& text)
        : m_id(id)
        , m_text(text)
    {
    }

    TextField& TextField::Hint(const char* text)
    {
        m_hint = text;
        return *this;
    }

    TextField& TextField::MaxLength(std::size_t length)
    {
        m_maxLength = length;
        return *this;
    }

    TextField& TextField::Multiline(bool multiline, float lines)
    {
        m_multiline = multiline;
        m_lines = lines;
        return *this;
    }

    TextField& TextField::CommitOnEnter(bool commit)
    {
        m_commitOnEnter = commit;
        return *this;
    }

    TextField& TextField::Invalid(bool invalid)
    {
        m_invalid = invalid;
        return *this;
    }

    TextField& TextField::Width(float width)
    {
        m_width = width;
        return *this;
    }

    bool TextField::Draw() const
    {
        char buffer[FieldCapacity] = {};
        std::size_t capacity = sizeof(buffer);
        if (m_maxLength != 0 && m_maxLength + 1 < capacity)
        {
            capacity = m_maxLength + 1;
        }
        const std::size_t copied =
            m_text.size() < capacity - 1 ? m_text.size() : capacity - 1;
        std::memcpy(buffer, m_text.c_str(), copied);

        InvalidScope invalid(m_invalid);
        if (m_width != 0.0f)
        {
            ImGui::SetNextItemWidth(m_width);
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
        if (m_commitOnEnter)
        {
            flags |= ImGuiInputTextFlags_EnterReturnsTrue;
        }

        bool changed = false;
        const char* id = m_id != nullptr ? m_id : "##text";
        if (m_multiline)
        {
            changed = ImGui::InputTextMultiline(id, buffer, capacity,
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * m_lines), flags);
        }
        else if (m_hint != nullptr)
        {
            changed = ImGui::InputTextWithHint(id, m_hint, buffer, capacity, flags);
        }
        else
        {
            changed = ImGui::InputText(id, buffer, capacity, flags);
        }

        if (changed)
        {
            m_text = buffer;
        }
        return changed;
    }

    bool TextField::operator()() const
    {
        return Draw();
    }
}
