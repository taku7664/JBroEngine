#include <JBro/Editor/Widget/TextField.h>

#include <JBro/Types/Array.h>
#include <JBro/Types/ValueMath.h>
#include <imgui_internal.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::Widget
{
    namespace
    {
        // 한 프레임 동안만 쓰는 버퍼다. 글자 칸은 한 번에 하나만 활성이므로
        // 하나면 된다 - 여러 칸을 같은 프레임에 그려도 각자 그릴 때만 쓴다.
        constexpr std::size_t FieldCapacity = 1024;

        // 여러 줄 칸은 길이에 끝이 없다(텍스트의 글자). ImGui 가 모자라다고 하면 이 버퍼를 늘린다.
        // 활성 칸은 하나이고 메인 스레드에서만 그리므로 하나를 같이 쓴다.
        Array<char>& GrowableBuffer()
        {
            static Array<char> buffer;
            return buffer;
        }

        int ResizeBuffer(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                Array<char>& buffer = GrowableBuffer();
                buffer.Resize(static_cast<std::size_t>(data->BufSize));
                data->Buf = buffer.Data();
            }
            return 0;
        }

        // 여러 줄 칸의 높이다. 글자 줄 수를 따라 늘되 `minimum` 아래로는 줄지 않고, 너무 길면 칸 안에서 굴린다.
        Float MultilineHeightInLines(const String& text, Float minimum, Float maximum)
        {
            Float lines = 1.0f;
            for (const char c : text)
            {
                if (c == '\n')
                {
                    lines += 1.0f;
                }
            }
            // 커서가 설 빈 줄 하나를 더 둔다.
            lines += 1.0f;
            if (lines < minimum)
            {
                lines = minimum;
            }
            return lines < maximum ? lines : maximum;
        }
    }

    TextField::TextField(const char* id, String& text)
        : m_id(id)
        , m_text(text)
    {
    }

    TextField& TextField::Password(Bool password)
    {
        m_password = password;
        return *this;
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

    TextField& TextField::Multiline(Bool multiline, Float lines)
    {
        m_multiline = multiline;
        m_lines = lines;
        return *this;
    }

    TextField& TextField::LiveText(Bool live)
    {
        m_liveText = live;
        return *this;
    }

    TextField& TextField::CommitOnEnter(Bool commit)
    {
        m_commitOnEnter = commit;
        return *this;
    }

    TextField& TextField::CommitOnFinish(Bool commit)
    {
        m_commitOnFinish = commit;
        return *this;
    }

    TextField& TextField::Invalid(Bool invalid)
    {
        m_invalid = invalid;
        return *this;
    }

    TextField& TextField::Width(Float width)
    {
        m_width = width;
        return *this;
    }

    Bool TextField::Draw() const
    {
        if (m_multiline && m_maxLength == 0)
        {
            return DrawGrowable();
        }
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

        ImGuiInputTextFlags flags = m_password ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None;
        if (m_commitOnEnter)
        {
            flags |= ImGuiInputTextFlags_EnterReturnsTrue;
            if (m_multiline)
            {
                flags |= ImGuiInputTextFlags_CtrlEnterForNewLine;
            }
        }
        if (m_multiline && m_wordWrap)
        {
            flags |= ImGuiInputTextFlags_WordWrap;
        }

        Bool changed = false;
        const char* id = m_id != nullptr ? m_id : "##text";
        if (m_multiline)
        {
            const Float visibleLines = m_maxVisibleLines > 0.0f && m_lines > m_maxVisibleLines ? m_maxVisibleLines : m_lines;
            changed = ImGui::InputTextMultiline(id, buffer, capacity,
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * visibleLines), flags);
        }
        else if (m_hint != nullptr)
        {
            changed = ImGui::InputTextWithHint(id, m_hint, buffer, capacity, flags);
        }
        else
        {
            changed = ImGui::InputText(id, buffer, capacity, flags);
        }

        if (changed || (m_liveText && std::strcmp(buffer, m_text.c_str()) != 0))
        {
            m_text = buffer;
        }
        if (m_commitOnFinish)
        {
            // 글자는 이미 들어갔다. 확정만 편집이 끝나는 프레임으로 미룬다.
            return ImGui::IsItemDeactivatedAfterEdit();
        }
        return changed;
    }

    TextField& TextField::MaxVisibleLines(Float lines)
    {
        m_maxVisibleLines = lines > 1.0f ? lines : Float(1.0f);
        return *this;
    }

    TextField& TextField::WordWrap(Bool wrap)
    {
        m_wordWrap = wrap;
        return *this;
    }

    Float TextField::GetMultilineHeight(Float width) const
    {
        const Float maximum = m_maxVisibleLines > 0.0f ? m_maxVisibleLines : Float(16.0f);
        Float lines = MultilineHeightInLines(m_text, m_lines, maximum);
        if (m_wordWrap)
        {
            const auto& style = ImGui::GetStyle();
            // Reserve the vertical scrollbar, just as InputTextMultiline does when wrapping.
            const Float wrapWidth = Max(width - style.FramePadding.x * 2.0f - style.ScrollbarSize - 2.0f, Float(1.0f));
            lines = 1.0f;
            const char* at = m_text.data();
            const char* end = at + m_text.size();
            while (at < end && lines < maximum)
            {
                const char* next = ImFontCalcWordWrapPositionEx(ImGui::GetFont(), ImGui::GetFontSize(),
                    at, end, wrapWidth.Value, ImDrawTextFlags_WrapKeepBlanks);
                if (next >= end)
                {
                    break;
                }
                if (next <= at)
                {
                    unsigned int codepoint = 0;
                    const Int32 bytes = ImTextCharFromUtf8(&codepoint, at, end);
                    next = at + Max(bytes, Int32(1)).Value;
                }
                at = *next == '\n' ? next + 1 : next;
                lines += 1.0f;
            }
            lines = Clamp(lines, Min(m_lines, maximum), maximum);
        }
        return ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0f;
    }

    Bool TextField::DrawGrowable() const
    {
        Array<char>& buffer = GrowableBuffer();
        buffer.Resize(m_text.size() + 1);
        std::memcpy(buffer.Data(), m_text.c_str(), m_text.size() + 1);

        InvalidScope invalid(m_invalid);
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize
            | (m_wordWrap ? ImGuiInputTextFlags_WordWrap : ImGuiInputTextFlags_None)
            | (m_commitOnEnter ? ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine
                : ImGuiInputTextFlags_None);
        const char* id = m_id != nullptr ? m_id : "##text";
        const Float width = m_width != 0.0f ? m_width : Float(-FLT_MIN);
        const Bool changed = ImGui::InputTextMultiline(id, buffer.Data(), buffer.Size(),
            ImVec2(width, GetMultilineHeight(m_width > 0.0f ? m_width : Float(ImGui::GetContentRegionAvail().x))),
            flags, ResizeBuffer);
        if (changed || (m_liveText && std::strcmp(buffer.Data(), m_text.c_str()) != 0))
        {
            m_text = buffer.Data();
        }
        if (m_commitOnFinish)
        {
            return ImGui::IsItemDeactivatedAfterEdit();
        }
        return changed;
    }

    Bool TextField::operator()() const
    {
        return Draw();
    }
}
