#include <JBro/Editor/Widget/TaskProgress.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Task/TaskManager.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace JBro::Widget
{
    namespace
    {
        const char* Translate(const String& key)
        {
            return Loc::TextOr(key.c_str(), key.c_str());
        }

        float Fraction(std::uint32_t done, std::uint32_t total)
        {
            return total == 0 ? 1.0f : static_cast<float>(done) / static_cast<float>(total);
        }

        ImVec4 StateColor(TaskState state)
        {
            switch (state)
            {
            case TaskState::Completed:
                return SeverityColor(Severity::Success);
            case TaskState::Failed:
                return SeverityColor(Severity::Error);
            case TaskState::Running:
                return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            default:
                return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
        }

        // 줄 머리 표시다. `LoadingSpinner`·`CheckMark` 와 같은 자리를 차지해 상태가 바뀌어도 줄이 흔들리지 않는다.
        void StateMark(TaskState state)
        {
            const ImVec4 color = StateColor(state);
            if (state == TaskState::Running)
            {
                LoadingSpinner(0.0f, color);
                return;
            }
            if (state == TaskState::Completed)
            {
                CheckMark(0.0f, color);
                return;
            }
            const float side = ImGui::GetFrameHeight();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(side, side));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 packed = ImGui::GetColorU32(color);
            const ImVec2 center(origin.x + side * 0.5f, origin.y + side * 0.5f);
            const float r = side * 0.5f - ImGui::GetStyle().FramePadding.y;
            if (state == TaskState::Failed)
            {
                draw->AddLine(ImVec2(center.x - r * 0.7f, center.y - r * 0.7f), ImVec2(center.x + r * 0.7f, center.y + r * 0.7f), packed, 2.0f);
                draw->AddLine(ImVec2(center.x - r * 0.7f, center.y + r * 0.7f), ImVec2(center.x + r * 0.7f, center.y - r * 0.7f), packed, 2.0f);
            }
            else if (state == TaskState::Canceled)
            {
                draw->AddLine(ImVec2(center.x - r * 0.7f, center.y), ImVec2(center.x + r * 0.7f, center.y), packed, 2.0f);
            }
            else
            {
                draw->AddCircle(center, r * 0.6f, packed, 0, 1.5f);
            }
        }

        void DrawCircle(float fraction, float diameter, const char* overlay)
        {
            if (diameter <= 0.0f)
            {
                diameter = ImGui::GetFrameHeight() * 2.0f;
            }
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(diameter, diameter));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float thickness = std::max(2.0f, diameter * 0.08f);
            const float radius = diameter * 0.5f - thickness * 0.5f;
            const ImVec2 center(origin.x + diameter * 0.5f, origin.y + diameter * 0.5f);
            draw->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_FrameBg), 0, thickness);
            const float start = -IM_PI * 0.5f;
            draw->PathArcTo(center, radius, start, start + IM_PI * 2.0f * std::clamp(fraction, 0.0f, 1.0f), 48);
            draw->PathStroke(ImGui::GetColorU32(ImGuiCol_PlotHistogram), ImDrawFlags_None, thickness);
            const ImVec2 textSize = ImGui::CalcTextSize(overlay);
            draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), overlay);
        }
    }

    TaskGroupSummary SummarizeTaskGroup(const TaskManager& tasks, TaskGroupId group)
    {
        TaskGroupSummary summary;
        const TaskGroup* found = tasks.FindGroup(group);
        if (found == nullptr)
        {
            return summary;
        }
        summary.found = true;
        summary.state = found->GetState();
        summary.nameKey = found->GetName().c_str();
        summary.taskCount = found->GetTaskCount();
        for (std::uint32_t index = 0; index < summary.taskCount; ++index)
        {
            const Task& task = found->GetTaskAt(index);
            summary.total += task.GetNumSubTasks();
            summary.failed += task.GetFailedSubTasks();
            summary.done += task.GetSucceededSubTasks() + task.GetFailedSubTasks();
        }
        return summary;
    }

    TaskProgress::TaskProgress(const TaskManager& tasks, TaskGroupId group)
        : m_tasks(tasks)
        , m_group(group)
    {
    }

    TaskProgress& TaskProgress::Bar()
    {
        m_circle = false;
        return *this;
    }

    TaskProgress& TaskProgress::Circle()
    {
        m_circle = true;
        return *this;
    }

    TaskProgress& TaskProgress::TaskList(bool show)
    {
        m_list = show;
        return *this;
    }

    TaskProgress& TaskProgress::Size(ImVec2 size)
    {
        m_size = size;
        return *this;
    }

    TaskProgress& TaskProgress::MaxRows(std::uint32_t rows)
    {
        m_maxRows = rows > 0 ? rows : 1;
        return *this;
    }

    bool TaskProgress::Draw() const
    {
        const TaskGroupSummary summary = SummarizeTaskGroup(m_tasks, m_group);
        if (false == summary.found)
        {
            return false;
        }
        const TaskGroup& group = *m_tasks.FindGroup(m_group);
        ImGui::PushID(static_cast<int>(m_group & 0x7FFFFFFF));
        char overlay[48];
        std::snprintf(overlay, sizeof(overlay), "%u / %u", summary.done, summary.total);
        const float fraction = Fraction(summary.done, summary.total);
        if (m_circle)
        {
            DrawCircle(fraction, m_size.x, overlay);
        }
        else
        {
            const float width = m_size.x > 0.0f ? m_size.x : -FLT_MIN;
            ImGui::ProgressBar(fraction, ImVec2(width, m_size.y), overlay);
        }
        if (m_list && summary.taskCount > 0)
        {
            const float rowHeight = ImGui::GetFrameHeightWithSpacing();
            const float rows = static_cast<float>(std::min(summary.taskCount, m_maxRows));
            if (ImGui::BeginChild("##TaskList", ImVec2(0.0f, rows * rowHeight), ImGuiChildFlags_None))
            {
                const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                for (std::uint32_t index = 0; index < summary.taskCount; ++index)
                {
                    const Task& task = group.GetTaskAt(index);
                    const TaskState state = task.GetState();
                    ImGui::PushID(static_cast<int>(index));
                    StateMark(state);
                    ImGui::SameLine(0.0f, spacing);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(StateColor(state), "%s", Translate(task.GetName()));
                    char count[32];
                    std::snprintf(count, sizeof(count), "%u/%u",
                        task.GetSucceededSubTasks() + task.GetFailedSubTasks(), task.GetNumSubTasks());
                    const float countWidth = ImGui::CalcTextSize(count).x;
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - countWidth));
                    ImGui::TextDisabled("%s", count);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::PopID();
        return true;
    }

    bool TaskProgress::operator()() const
    {
        return Draw();
    }

    bool TaskStatusItem(const TaskManager& tasks, TaskGroupId group, std::uint32_t othersRunning, float barWidth)
    {
        const TaskGroupSummary summary = SummarizeTaskGroup(tasks, group);
        if (false == summary.found)
        {
            return false;
        }
        ImGui::BeginGroup();
        LoadingSpinner();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Loc::TextOr(summary.nameKey, summary.nameKey));
        ImGui::SameLine();
        TaskProgress(tasks, group).Size(ImVec2(barWidth, 0.0f)).Draw();
        if (othersRunning > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("+%u", othersRunning);
        }
        ImGui::EndGroup();
        return ImGui::IsItemClicked(ImGuiMouseButton_Left);
    }

    bool StatusMessage(const char* text, Severity severity)
    {
        if (IsEmptyText(text))
        {
            return false;
        }
        const float width = ImGui::CalcTextSize(text).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(SeverityColor(severity), "%s", text);
        return ImGui::IsItemClicked(ImGuiMouseButton_Left);
    }

    bool TaskGroupSection(const TaskManager& tasks, TaskGroupId group)
    {
        const TaskGroupSummary summary = SummarizeTaskGroup(tasks, group);
        if (false == summary.found)
        {
            return false;
        }
        ImGui::TextUnformatted(Loc::TextOr(summary.nameKey, summary.nameKey));
        return TaskProgress(tasks, group).TaskList().Draw();
    }
}
