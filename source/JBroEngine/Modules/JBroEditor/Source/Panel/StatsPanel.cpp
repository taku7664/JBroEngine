#include "StatsPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/Meter.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Audio/AudioMixer.h>
#include <JBro/Audio/AudioSystem.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui.h>

namespace JBro
{
    // 버스마다 미터와 솔로다(D-203). 미터는 봉우리를 잡고 초당 1.5 씩 내린다 - 한 블록만 보면 읽을 새가 없다.
    void StatsPanel::DrawAudioMeters(System::AudioSystem& audio, float masterPeak)
    {
        const auto hold = [](float& shown, float now) {
            const float fallen = shown - 1.5f * ImGui::GetIO().DeltaTime;
            shown = now > fallen ? now : (fallen > 0.0f ? fallen : 0.0f);
        };
        Widget::FormLayout meters("##audioMeters");
        hold(m_masterLevel, masterPeak);
        meters.Row([] { Widget::Text(AudioMasterBusName); },
            [&] { Widget::LevelMeter("##master", m_masterLevel); });
        const JArrayView<AudioBusConfig> buses = audio.GetBusConfigs();
        for (std::uint32_t index = 0; index < buses.size && index < MaxMeteredBuses; ++index)
        {
            const AudioBusConfig& config = buses.data[index];
            AudioBusName name;
            name.id = config.name;
            hold(m_busLevels[index], audio.GetBusPeak(name));
            // 번호는 값 칸 안에서만 민다 - 표의 줄 사이에서 밀면 표의 ID 쌓기가 어긋난다.
            meters.Row([&config] { Widget::Text(NameTable::Get().Resolve(config.name)); },
                [&] {
                    ImGui::PushID(static_cast<int>(index));
                    bool solo = audio.IsBusSolo(name);
                    if (Widget::Checkbox("##solo", solo))
                    {
                        audio.SetBusSolo(name, solo);
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::StatsAudioSolo, "Solo - hear only this bus while mixing"));
                    ImGui::SameLine();
                    Widget::LevelMeter("##level", m_busLevels[index]);
                    ImGui::PopID();
                });
        }
    }

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

        // **소리가 얼마나 쓰이는지**(D-197, 기존 백로그의 오디오 프로파일러 자리). 보이스가 모자라 훔치거나 거절하면 소리가
        // 조용히 사라진다 - 그것이 보여야 한다.
        if (System::AudioSystem* audio = m_editor->GetAudio())
        {
            if (const AudioMixer* mixer = audio->GetMixer())
            {
                const AudioMixer::Stats sound = mixer->GetStats();
                ImGui::Separator();
                if (const char* device = m_editor->GetAudioDeviceName())
                {
                    Widget::TextF(Loc::TextOr(LocKeys::StatsAudioDevice, "audio device %s"), device);
                }
                else
                {
                    Widget::HintTextF("%s", Loc::TextOr(LocKeys::StatsAudioNoDevice,
                        "no audio device - sounds are mixed but not heard"));
                }
                Widget::TextF(Loc::TextOr(LocKeys::StatsAudioVoices, "voices %u / %u, peak %.2f"),
                    sound.activeVoices, sound.maxVoices, static_cast<double>(sound.lastPeak));
                if (sound.voicesStolen != 0 || sound.voicesRejected != 0)
                {
                    Widget::SeverityTextF(Widget::Severity::Warning,
                        Loc::TextOr(LocKeys::StatsAudioStolen,
                            "%llu voice(s) stolen, %llu refused - raise the voice count or lower priorities"),
                        static_cast<unsigned long long>(sound.voicesStolen),
                        static_cast<unsigned long long>(sound.voicesRejected));
                }
                // 버스마다 미터와 솔로다(D-203). 미터는 봉우리를 잡고 초당 1.5 씩 내린다.
                if (Widget::FoldNode(Loc::TextOr(LocKeys::StatsAudioBuses, "Buses"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // 표는 마디를 닫기 전에 끝나야 한다 - 그래서 제 함수 안에서 연다.
                    DrawAudioMeters(*audio, sound.lastPeak);
                    Widget::TreePop();
                }
                mixer->ComputeSpectrum(m_spectrum, SpectrumBands);
                Widget::Spectrum("##spectrum", {m_spectrum, SpectrumBands}, ImGui::GetFrameHeight() * 2.5f);
            }
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
