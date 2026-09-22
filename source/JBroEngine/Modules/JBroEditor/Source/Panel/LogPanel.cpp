#include "LogPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>

#include <imgui.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        // 등급마다 글자색을 달리한다. 경고와 오류가 눈에 먼저 들어와야 한다.
        ImVec4 LevelColor(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace: return ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
            case LogLevel::Debug: return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
            case LogLevel::Info: return ImVec4(0.55f, 0.78f, 1.00f, 1.0f);
            case LogLevel::Warning: return ImVec4(1.00f, 0.78f, 0.26f, 1.0f);
            case LogLevel::Error: return ImVec4(1.00f, 0.38f, 0.33f, 1.0f);
            default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        const char* LevelLabel(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace: return Loc::TextOr(LocKeys::LogTrace, "Trace");
            case LogLevel::Debug: return Loc::TextOr(LocKeys::LogDebug, "Debug");
            case LogLevel::Info: return Loc::TextOr(LocKeys::LogInfo, "Info");
            case LogLevel::Warning: return Loc::TextOr(LocKeys::LogWarning, "Warning");
            case LogLevel::Error: return Loc::TextOr(LocKeys::LogError, "Error");
            default: return "";
            }
        }

        // 대소문자를 가리지 않는 부분 일치. 계층의 검색과 같은 규칙이다.
        bool ContainsFold(const char* text, const String& needle)
        {
            if (needle.size() == 0)
            {
                return true;
            }
            if (text == nullptr)
            {
                return false;
            }
            for (const char* at = text; *at != '\0'; ++at)
            {
                std::size_t index = 0;
                while (index < needle.size() && at[index] != '\0')
                {
                    const char left = at[index];
                    const char right = needle.c_str()[index];
                    const char lowerLeft = (left >= 'A' && left <= 'Z')
                        ? static_cast<char>(left - 'A' + 'a') : left;
                    const char lowerRight = (right >= 'A' && right <= 'Z')
                        ? static_cast<char>(right - 'A' + 'a') : right;
                    if (lowerLeft != lowerRight)
                    {
                        break;
                    }
                    ++index;
                }
                if (index == needle.size())
                {
                    return true;
                }
            }
            return false;
        }
    }

    const char* LogPanel::GetTitle() const
    {
        return "Log";
    }

    const char* LogPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelLog, "Log");
    }

    bool LogPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    bool LogPanel::Passes(const LogEntry& entry) const
    {
        const std::size_t level = static_cast<std::size_t>(entry.level);
        if (level >= sizeof(m_levels) / sizeof(m_levels[0]) || false == m_levels[level])
        {
            return false;
        }
        // 찾는 글자는 **본문과 갈래 둘 다**에 건다. `asset` 만 쳐서 그 갈래를 모아 보는
        // 것이 흔한 쓰임이다.
        return ContainsFold(entry.message, m_filter) || ContainsFold(entry.category, m_filter);
    }

    void LogPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        // 새 줄이 왔을 때만 바닥으로 따라간다. 판번호가 그대로면 사람이 스크롤한 자리를
        // 그대로 둔다 - 매 프레임 바닥으로 끌면 위로 올려 읽을 수가 없다.
        const std::uint64_t revision = Log::GetRevision();
        if (revision != m_seenRevision)
        {
            m_seenRevision = revision;
            m_scrollToBottom = m_autoScroll;
        }

        DrawToolBar();
        ImGui::Separator();
        DrawEntries();
    }

    void LogPanel::DrawToolBar()
    {
        if (Widget::Button(Loc::TextOr(LocKeys::LogClear, "Clear")))
        {
            Log::Clear();
        }
        ImGui::SameLine(0.0f, 6.0f);
        // 이름표는 칸 옆 글자다. 위젯에 넘기면 그 글자가 Id 가 되어 번역이 바뀔 때 상태가 풀린다.
        Widget::Checkbox("##follow", m_autoScroll);
        ImGui::SameLine(0.0f, 4.0f);
        Widget::Text(Loc::TextOr(LocKeys::LogAutoScroll, "Follow"));
        Widget::ToolBarSeparator();

        // 등급 토글. `Trace` 는 켤 일이 드물어 기본이 꺼짐이다.
        static constexpr LogLevel Levels[] = {
            LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
            LogLevel::Warning, LogLevel::Error};
        for (std::size_t index = 0; index < sizeof(Levels) / sizeof(Levels[0]); ++index)
        {
            if (index != 0)
            {
                ImGui::SameLine(0.0f, 6.0f);
            }
            const std::size_t slot = static_cast<std::size_t>(Levels[index]);
            Widget::IdScope id(static_cast<int>(slot));
            Widget::Checkbox("##level", m_levels[slot]);
            ImGui::SameLine(0.0f, 4.0f);
            Widget::StyleScope style;
            style.PushColor(ImGuiCol_Text, LevelColor(Levels[index]));
            Widget::Text(LevelLabel(Levels[index]));
        }

        ImGui::SameLine(0.0f, 12.0f);
        Widget::SearchBox("##logfilter", m_filter)
            .Hint(Loc::TextOr(LocKeys::CommonSearch, "Search"))
            .Width(180.0f)
            .Draw();
    }

    void LogPanel::DrawEntries()
    {
        if (false == ImGui::BeginChild("##entries", ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::EndChild();
            return;
        }

        const std::size_t count = Log::GetCount();
        std::size_t shown = 0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const LogEntry* entry = Log::GetAt(index);
            if (entry == nullptr || false == Passes(*entry))
            {
                continue;
            }
            ++shown;
            // 줄마다 번호로 Id 를 준다. 같은 글자가 두 번 나와도 둘이 한 줄을 나눠 쓰지 않는다.
            ImGui::PushID(static_cast<int>(entry->serial));
            Widget::StyleScope style;
            style.PushColor(ImGuiCol_Text, LevelColor(entry->level));
            if (entry->category[0] != '\0')
            {
                Widget::TextF("[%s] %s", entry->category, entry->message);
            }
            else
            {
                Widget::Text(entry->message);
            }
            ImGui::PopID();
        }

        if (shown == 0)
        {
            Widget::HintText(count == 0
                ? Loc::TextOr(LocKeys::LogEmpty, "nothing has been logged")
                : Loc::TextOr(LocKeys::CommonNoMatches, "no matches"));
        }

        if (m_scrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            m_scrollToBottom = false;
        }
        ImGui::EndChild();
    }
}
