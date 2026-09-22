#include "StatsPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Graphics/Renderer.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui.h>

namespace JBro
{
    const char* StatsPanel::GetTitle() const
    {
        // 안정된 이름이다. 번역하지 않는다 - 창의 정체가 여기 달려 있다.
        return "Stats";
    }

    const char* StatsPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelStats, "Stats");
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

        Widget::TextF(Loc::TextOr(LocKeys::StatsFrameTime, "frame %.2f ms"),
            average * 1000.0f);
        Widget::TextF(Loc::TextOr(LocKeys::StatsPerSecond, "%.0f per second"),
            average > 0.0f ? 1.0f / average : 0.0f);
        ImGui::Separator();
        Widget::TextF(Loc::TextOr(LocKeys::StatsFrameCount, "frames %llu"),
            static_cast<unsigned long long>(m_frames));

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
        Widget::TextF(Loc::TextOr(LocKeys::StatsViews, "views %u"), stats.viewCount);
        Widget::TextF(Loc::TextOr(LocKeys::StatsSprites, "sprites %u"), stats.spriteCount);
        if (stats.droppedViewCount != 0 || stats.droppedSpriteCount != 0)
        {
            // 넘치면 조용히 버려진다. 버려진 것이 있으면 그것부터 보여야 한다.
            Widget::SeverityTextF(Widget::Severity::Warning,
                Loc::TextOr(LocKeys::StatsDropped, "dropped %u view(s), %u sprite(s)"),
                stats.droppedViewCount, stats.droppedSpriteCount);
        }

        // **캔버스가 얼마나 찼는지**(D-145). 기존 엔진의 CPU 프로파일러가 이 숫자들을 냈다.
        // 오브젝트가 몇인지, 고른 것이 몇인지, 되돌릴 것이 남았는지 - 화면에 없으면
        // 캔버스가 무거워진 까닭을 짐작으로 찾게 된다.
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas != nullptr)
        {
            ImGui::Separator();
            std::size_t objectLive = 0;
            std::size_t objectCapacity = 0;
            canvas->GetObjectPoolUsage(objectLive, objectCapacity);
            Widget::TextF(Loc::TextOr(LocKeys::StatsObjects, "objects %llu / %llu"),
                static_cast<unsigned long long>(objectLive),
                static_cast<unsigned long long>(objectCapacity));
            Widget::TextF(Loc::TextOr(LocKeys::StatsLayers, "layers %llu"),
                static_cast<unsigned long long>(canvas->GetLayerCount()));
            Widget::TextF(Loc::TextOr(LocKeys::StatsSelected, "selected %llu"),
                static_cast<unsigned long long>(m_editor->GetSelectionCount()));

            // 컴포넌트 풀은 타입마다 따로 산다. **늘어나는 순간이 프레임을 늘어지게 만드는
            // 자리**라, 얼마나 남았는지가 보여야 한다.
            if (Widget::Tree(Loc::TextOr(LocKeys::StatsPools, "component pools"),
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool any = false;
                canvas->ForEachComponentPool([&](const Canvas::ComponentPoolUsage& usage) {
                    any = true;
                    const char* typeName = NameTable::Get().Resolve(usage.typeId);
                    Widget::TextF("%s  %llu / %llu",
                        typeName != nullptr ? typeName : "?",
                        static_cast<unsigned long long>(usage.live),
                        static_cast<unsigned long long>(usage.capacity));
                });
                if (false == any)
                {
                    Widget::HintTextF("%s",
                        Loc::TextOr(LocKeys::StatsNoPools, "no component pool has been made yet"));
                }
                Widget::TreePop();
            }
        }

        // 되돌리기의 상태. 기존도 같은 세 줄을 냈다.
        ImGui::Separator();
        const EditorCommandManager& commands = m_editor->GetCommands();
        Widget::TextF(Loc::TextOr(LocKeys::StatsUndo, "undo %llu / redo %llu"),
            static_cast<unsigned long long>(commands.GetUndoCount()),
            static_cast<unsigned long long>(commands.GetRedoCount()));
        Widget::TextF("%s", commands.IsDirty()
            ? Loc::TextOr(LocKeys::StatsDirty, "there are unsaved changes")
            : Loc::TextOr(LocKeys::StatsClean, "everything is saved"));
    }
}
