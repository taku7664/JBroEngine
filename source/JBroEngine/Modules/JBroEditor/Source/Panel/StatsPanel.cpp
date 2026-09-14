#include "StatsPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Graphics/Renderer.h>

#include <imgui.h>

namespace JBro
{
    const char* StatsPanel::GetTitle() const
    {
        return "Stats";
    }

    bool StatsPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void StatsPanel::OnUpdate(float deltaTime)
    {
        // **닫혀 있어도 센다.** 패널을 열어 본 순간의 숫자가 그 순간부터 모은 것이면
        // 열어 보는 행위가 측정을 바꾼다.
        ++m_frames;
        m_samples[m_nextSample] = deltaTime;
        m_nextSample = (m_nextSample + 1) % SampleCount;
        if (m_filledSamples < SampleCount)
        {
            ++m_filledSamples;
        }
    }

    void StatsPanel::OnDraw()
    {
        float total = 0.0f;
        for (int index = 0; index < m_filledSamples; ++index)
        {
            total += m_samples[index];
        }
        const float average = m_filledSamples > 0
            ? total / static_cast<float>(m_filledSamples)
            : 0.0f;

        ImGui::Text("frame %.2f ms", average * 1000.0f);
        ImGui::Text("%.0f per second", average > 0.0f ? 1.0f / average : 0.0f);
        ImGui::Separator();
        ImGui::Text("frames %llu", static_cast<unsigned long long>(m_frames));

        if (m_editor == nullptr)
        {
            return;
        }
        const Renderer* renderer = m_editor->GetRenderer();
        if (renderer == nullptr)
        {
            return;
        }
        const RendererFrameStats stats = renderer->GetLastFrameStats();
        ImGui::Separator();
        ImGui::Text("views %u", stats.viewCount);
        ImGui::Text("sprites %u", stats.spriteCount);
        if (stats.droppedViewCount != 0 || stats.droppedSpriteCount != 0)
        {
            // 넘치면 조용히 버려진다. 버려진 것이 있으면 그것부터 보여야 한다.
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "dropped %u view(s), %u sprite(s)",
                stats.droppedViewCount, stats.droppedSpriteCount);
        }
    }
}
