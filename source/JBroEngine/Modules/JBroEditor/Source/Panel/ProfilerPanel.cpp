#include "ProfilerPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Common.h>

#include <imgui.h>

namespace JBro
{
    namespace
    {
        // 한 프레임의 숫자는 널뛴다. 새 값을 이만큼만 섞어 눌러 준다 - 0.1 이면
        // 대략 열 프레임에 걸쳐 따라간다.
        constexpr double Smoothing = 0.1;

        double ToMilliseconds(std::uint64_t nanoseconds)
        {
            return static_cast<double>(nanoseconds) / 1000000.0;
        }
    }

    const char* ProfilerPanel::GetTitle() const
    {
        return "Profiler";
    }

    const char* ProfilerPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelProfiler, "Profiler");
    }

    bool ProfilerPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        // 늘 보는 창이 아니다. 창 메뉴에서 열어 본다.
        SetOpen(false);
        return true;
    }

    void ProfilerPanel::OnDestroy()
    {
        // 창이 사라지면 재는 것도 끈다. 재는 것 자체가 프레임에 얹히는 일이다.
        Profiler::SetEnabled(false);
    }

    void ProfilerPanel::OnUpdate(float deltaTime)
    {
        (void)deltaTime;
        // **창이 보일 때만 잰다.** 닫아 둔 창을 위해 매 프레임 시계를 읽을 이유가 없다.
        Profiler::SetEnabled(IsOpen());
        if (false == IsOpen())
        {
            m_rowCount = 0;
            m_frameMilliseconds = 0.0;
            return;
        }

        // 지난 프레임의 줄을 지금 든 줄과 이름·겹으로 맞춰 눌러 준다. 순서가 바뀌면
        // 새 줄로 친다 - 프레임마다 구간이 달라지는 것은 그 자체로 볼 만한 일이다.
        const std::size_t count = Profiler::GetCount();
        for (std::size_t index = 0; index < count && index < Profiler::MaxSamples; ++index)
        {
            const ProfileSample* sample = Profiler::GetAt(index);
            if (sample == nullptr)
            {
                continue;
            }
            const double now = ToMilliseconds(sample->totalNanoseconds);
            Smoothed& row = m_rows[index];
            const bool same = row.name == sample->name && row.depth == sample->depth;
            row.name = sample->name;
            row.depth = sample->depth;
            row.callCount = sample->callCount;
            row.milliseconds = same
                ? row.milliseconds + (now - row.milliseconds) * Smoothing
                : now;
        }
        m_rowCount = count < Profiler::MaxSamples ? count : Profiler::MaxSamples;

        // **첫 값은 누르지 않는다.** 0 에서 눌러 가면 처음 열 프레임 동안 프레임 시간이
        // 실제보다 작게 나오고, 구간의 비중이 100 을 훌쩍 넘어 보인다. 줄도 같은 규칙이다.
        const double frame = ToMilliseconds(Profiler::GetFrameNanoseconds());
        m_frameMilliseconds = m_frameMilliseconds > 0.0
            ? m_frameMilliseconds + (frame - m_frameMilliseconds) * Smoothing
            : frame;
    }

    void ProfilerPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        Widget::TextF("%s", Loc::TextOr(LocKeys::ProfilerHint,
            "measured while this window is open"));
        ImGui::Separator();

        if (m_rowCount == 0)
        {
            Widget::HintTextF("%s",
                Loc::TextOr(LocKeys::ProfilerEmpty, "nothing has been measured yet"));
            return;
        }

        // 구간 이름·시간·횟수·프레임에서 차지한 몫. **몫은 프레임 전체 대비**라
        // 겹이 다른 줄끼리 더해도 100 을 넘지 않는다.
        if (false == ImGui::BeginTable("##profile", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                    | ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::ProfilerSection, "Section"),
            ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::ProfilerTime, "ms"),
            ImGuiTableColumnFlags_WidthStretch, 0.2f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::ProfilerCalls, "calls"),
            ImGuiTableColumnFlags_WidthStretch, 0.15f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::ProfilerShare, "share"),
            ImGuiTableColumnFlags_WidthStretch, 0.2f);
        ImGui::TableHeadersRow();

        for (std::size_t index = 0; index < m_rowCount; ++index)
        {
            const Smoothed& row = m_rows[index];
            if (row.name == nullptr)
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // 겹은 들여쓰기로 보인다. 나무를 그리지 않아도 안팎이 읽힌다.
            const float indent = static_cast<float>(row.depth) * 14.0f;
            if (indent > 0.0f)
            {
                ImGui::Indent(indent);
            }
            Widget::Text(row.name);
            if (indent > 0.0f)
            {
                ImGui::Unindent(indent);
            }
            ImGui::TableSetColumnIndex(1);
            Widget::TextF("%.3f", row.milliseconds);
            ImGui::TableSetColumnIndex(2);
            Widget::TextF("%u", row.callCount);
            ImGui::TableSetColumnIndex(3);
            const double share = m_frameMilliseconds > 0.0001
                ? row.milliseconds / m_frameMilliseconds * 100.0
                : 0.0;
            Widget::TextF("%.1f%%", share);
        }
        ImGui::EndTable();

        ImGui::Spacing();
        Widget::TextF("%s %.3f ms",
            Loc::TextOr(LocKeys::ProfilerFrame, "frame"), m_frameMilliseconds);
    }
}
