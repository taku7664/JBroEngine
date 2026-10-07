#include "SoundEffectPanel.h"

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Audio/AudioEffectResponse.h>
#include <JBro/Audio/AudioSystem.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/AssetField.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Chips.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/Graph.h>
#include <JBro/Editor/Widget/Meter.h>
#include <JBro/Editor/Widget/Scalar.h>

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    const char* SoundEffectPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* SoundEffectPanel::GetDisplayTitle() const
    {
        return m_title.empty() ? Loc::TextOr(LocKeys::SoundEffectTitle, "Sound Effect") : m_title.c_str();
    }

    Bool SoundEffectPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void SoundEffectPanel::OnDestroy()
    {
        StopPreview();
    }

    Bool SoundEffectPanel::HasMenuBar() const
    {
        return true;
    }

    Bool SoundEffectPanel::Show(AssetId asset)
    {
        const AssetRecord* record = m_editor != nullptr ? m_editor->GetAssetRegistry().Find(asset) : nullptr;
        if (record == nullptr || record->type != AssetType::AudioEffect)
        {
            return false;
        }
        AudioEffectSettings read;
        if (false == m_editor->ReadSoundEffect(asset, read))
        {
            return false;
        }
        m_asset = asset;
        m_path = record->relativePath;
        m_draft = read;
        m_saved = read;
        m_message.clear();
        UpdateTitle();
        return true;
    }

    Bool SoundEffectPanel::IsDirty() const
    {
        if (m_draft.kind != m_saved.kind)
        {
            return true;
        }
        for (UInt32 index = 0; index < AudioEffectMaxParams; ++index)
        {
            if (m_draft.values[index] != m_saved.values[index])
            {
                return true;
            }
        }
        return false;
    }

    void SoundEffectPanel::UpdateTitle()
    {
        m_title = m_path;
        if (IsDirty())
        {
            m_title.append(" *", 2);
        }
    }

    Bool SoundEffectPanel::Save()
    {
        if (m_asset.IsNull() || false == m_editor->WriteSoundEffect(m_asset, m_draft))
        {
            m_message = Loc::TextOr(LocKeys::SoundEffectSaveFailed, "The sound effect could not be saved");
            m_messageIsError = true;
            return false;
        }
        m_saved = m_draft;
        m_message.clear();
        UpdateTitle();
        return true;
    }

    Bool SoundEffectPanel::ReloadFromDisk()
    {
        AudioEffectSettings read;
        if (m_asset.IsNull() || false == m_editor->ReadSoundEffect(m_asset, read))
        {
            m_message = Loc::TextOr(LocKeys::SoundEffectReadFailed, "The sound effect file could not be read");
            m_messageIsError = true;
            return false;
        }
        m_draft = read;
        m_saved = read;
        m_message.clear();
        UpdateTitle();
        PushDraftToPreview();
        return true;
    }

    void SoundEffectPanel::SetKind(AudioEffectKind kind)
    {
        m_draft = ChangeAudioEffectKind(m_draft, kind);
        UpdateTitle();
        PushDraftToPreview();
    }

    void SoundEffectPanel::SetParameter(UInt32 index, Float value)
    {
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(m_draft.kind);
        if (index >= info.paramCount)
        {
            return;
        }
        const AudioEffectParam& param = info.params[index];
        m_draft.values[index] = value < param.minValue ? param.minValue : (value > param.maxValue ? param.maxValue : value);
        UpdateTitle();
        PushDraftToPreview();
    }

    Bool SoundEffectPanel::IsPreviewing() const
    {
        System::AudioSystem* audio = m_editor != nullptr ? m_editor->GetAudio() : nullptr;
        return m_previewing && audio != nullptr && audio->IsPreviewPlaying();
    }

    Bool SoundEffectPanel::StartPreview()
    {
        System::AudioSystem* audio = m_editor->GetAudio();
        AssetSystem* assets = m_editor->GetAssetSystem();
        if (audio == nullptr || assets == nullptr || m_testSound.IsNull())
        {
            return false;
        }
        const AssetHandle held = assets->Load(m_testSound);
        m_previewing = audio->PlayPreview(held, m_loop, m_bypass ? nullptr : &m_draft, m_bypass ? 0u : 1u);
        // 붙잡지 않는다 - 인스펙터의 미리 듣기와 같다. 내려가면 해제 알림이 미리 듣기를 끊는다.
        assets->Release(held);
        return m_previewing;
    }

    void SoundEffectPanel::StopPreview()
    {
        System::AudioSystem* audio = m_editor != nullptr ? m_editor->GetAudio() : nullptr;
        if (IsPreviewing())
        {
            audio->StopPreview();
        }
        m_previewing = false;
    }

    void SoundEffectPanel::PushDraftToPreview()
    {
        if (IsPreviewing())
        {
            m_editor->GetAudio()->SetPreviewEffects(m_bypass ? nullptr : &m_draft, m_bypass ? 0u : 1u);
        }
    }

    void SoundEffectPanel::Revert()
    {
        m_draft = m_saved;
        m_message.clear();
        UpdateTitle();
        PushDraftToPreview();
    }

    void SoundEffectPanel::SetBypass(Bool bypass)
    {
        m_bypass = bypass;
        PushDraftToPreview();
    }

    void SoundEffectPanel::RefreshChoices()
    {
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        if (m_choicesBuilt && m_choicesRevision == registry.GetRevision())
        {
            return;
        }
        m_choicesBuilt = true;
        m_choicesRevision = registry.GetRevision();
        m_soundNames.Clear();
        m_soundIds.Clear();
        m_effectNames.Clear();
        m_effectIds.Clear();
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const AssetRecord& record = registry.GetRecord(index);
            if (record.type == AssetType::Audio)
            {
                m_soundNames.Add(record.relativePath);
                m_soundIds.Add(record.id);
            }
            else if (record.type == AssetType::AudioEffect)
            {
                m_effectNames.Add(record.relativePath);
                m_effectIds.Add(record.id);
            }
        }
        // 이름을 다 모은 뒤에 가리킨다. 모으는 중에 배열이 자라면 앞의 포인터가 무효가 된다.
        m_soundNamePointers.Clear();
        for (const String& name : m_soundNames)
        {
            m_soundNamePointers.Add(name.c_str());
        }
    }

    void SoundEffectPanel::OnMenuBar()
    {
        if (false == Widget::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            return;
        }
        RefreshChoices();
        // 기존의 "Open Sound Effect..." 다. 파일 고르기 대신 프로젝트의 `.jfx` 목록에서 고른다 - 에셋은 이미 등록돼 있다.
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::SoundEffectOpen, "Open Sound Effect")))
        {
            if (m_effectIds.IsEmpty())
            {
                Widget::HintText(Loc::TextOr(LocKeys::SoundEffectNone, "There are no sound effects in this project"));
            }
            for (std::size_t index = 0; index < m_effectIds.Size(); ++index)
            {
                if (Widget::MenuItem(m_effectNames[index].c_str()))
                {
                    m_editor->OpenSoundEffectEditor(m_effectIds[index]);
                }
            }
            Widget::EndMenu();
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::SoundEffectSave, "Save"), nullptr, IsDirty(),
                Loc::TextOr(LocKeys::SoundEffectNothingToSave, "nothing has changed")))
        {
            Save();
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::SoundEffectReload, "Reload From Disk")))
        {
            ReloadFromDisk();
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::SoundEffectClose, "Close")))
        {
            m_editor->ClosePanel(*this);
        }
        Widget::EndMenu();
    }

    namespace
    {
        const char* AboutKey(AudioEffectKind kind)
        {
            switch (kind)
            {
                case AudioEffectKind::Reverb:
                    return LocKeys::SoundEffectAboutReverb;
                case AudioEffectKind::LowPass:
                    return LocKeys::SoundEffectAboutLowPass;
                case AudioEffectKind::HighPass:
                    return LocKeys::SoundEffectAboutHighPass;
                case AudioEffectKind::Echo:
                    return LocKeys::SoundEffectAboutEcho;
                case AudioEffectKind::Distortion:
                    return LocKeys::SoundEffectAboutDistortion;
                case AudioEffectKind::Compressor:
                    return LocKeys::SoundEffectAboutCompressor;
                case AudioEffectKind::Limiter:
                    return LocKeys::SoundEffectAboutLimiter;
                case AudioEffectKind::Equalizer:
                    return LocKeys::SoundEffectAboutEqualizer;
                case AudioEffectKind::Chorus:
                    return LocKeys::SoundEffectAboutChorus;
                case AudioEffectKind::PitchShift:
                    return LocKeys::SoundEffectAboutPitchShift;
                default:
                    return LocKeys::SoundEffectAboutReverb;
            }
        }

        // 그래프의 눈금이다. 글자는 값이라 번역하지 않는다.
        constexpr Widget::GraphTick FrequencyTicks[] = {{100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}};
        constexpr Widget::GraphTick FilterGainTicks[] = {{-24.0f, "-24"}, {-12.0f, "-12"}, {0.0f, "0"}, {12.0f, "+12"}};
        constexpr Widget::GraphTick EqGainTicks[] = {{-12.0f, "-12"}, {0.0f, "0"}, {12.0f, "+12"}};
        constexpr Widget::GraphTick UnitTicks[] = {{-1.0f, "-1"}, {0.0f, "0"}, {1.0f, "1"}};
        constexpr Widget::GraphTick LevelTicks[] = {{-48.0f, "-48"}, {-36.0f, "-36"}, {-24.0f, "-24"}, {-12.0f, "-12"}, {0.0f, "0"}};
        constexpr Widget::GraphTick AmountTicks[] = {{0.0f, "0"}, {0.5f, "0.5"}, {1.0f, "1"}};
        constexpr Widget::GraphTick SecondsTicks[] = {{0.0f, "0"}, {0.5f, "0.5"}, {1.0f, "1"}, {1.5f, "1.5"}, {2.0f, "2"}, {2.5f, "2.5"}, {3.0f, "3"}};
        constexpr Widget::GraphTick MillisecondTicks[] = {{0.0f, "0"}, {10.0f, "10"}, {20.0f, "20"}};
        // 응답을 재는 샘플 레이트다. 장치 레이트와 달라도 20 kHz 아래의 모양은 거의 같다.
        constexpr Float GraphSampleRate = 48000.0f;
    }

    void SoundEffectPanel::DrawKind()
    {
        // **종류는 칩으로 고른다**(D-309). 열 가지가 한눈에 보여야 무엇을 고를 수 있는지 안다 - 콤보는 지금 것 하나만 보였다.
        // 이름은 파일의 `Kind` 값 그대로다(번역하지 않는다).
        const char* names[static_cast<std::size_t>(AudioEffectKind::Count)] = {};
        for (std::size_t index = 0; index < static_cast<std::size_t>(AudioEffectKind::Count); ++index)
        {
            names[index] = AudioEffectKinds[index].name;
        }
        Int32 current = static_cast<JBro::Int32>(static_cast<std::uint8_t>(m_draft.kind));
        if (Widget::ChoiceChips("##kind", {names, static_cast<std::size_t>(AudioEffectKind::Count)}, current) && current >= 0)
        {
            SetKind(static_cast<AudioEffectKind>(current.Get()));
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::SoundEffectKindHelp,
            "Changing the kind keeps the values of the parameters both kinds have"));
        Widget::HintText(Loc::Text(AboutKey(m_draft.kind)));
    }

    void SoundEffectPanel::DrawGraph()
    {
        // **그래프는 소리를 내는 식으로 잰다**(`AudioEffectResponse.h`). 따로 그린 그림이면 슬라이더를 움직일 때 소리와 그림이 어긋난다.
        const Float* v = m_draft.values;
        Widget::GraphDesc desc;
        Widget::GraphSeries series[2];
        UInt32 seriesCount = 0;
        Float markers[3] = {};
        UInt32 markerCount = 0;
        const char* caption = nullptr;
        UInt32 points = GraphPoints;
        switch (m_draft.kind)
        {
            case AudioEffectKind::LowPass:
            case AudioEffectKind::HighPass:
            case AudioEffectKind::Equalizer:
            {
                const Bool eq = m_draft.kind == AudioEffectKind::Equalizer;
                caption = Loc::TextOr(LocKeys::SoundEffectGraphFrequency, "Frequency response (dB)");
                desc.x = {20.0f, 20000.0f, true, {FrequencyTicks, std::size(FrequencyTicks)}};
                desc.y = eq ? Widget::GraphAxis{-24.0f, 24.0f, false, {EqGainTicks, std::size(EqGainTicks)}}
                            : Widget::GraphAxis{-36.0f, 24.0f, false, {FilterGainTicks, std::size(FilterGainTicks)}};
                for (UInt32 index = 0; index < points; ++index)
                {
                    const Float unit = static_cast<JBro::Float>(index) / static_cast<JBro::Float>(points - 1);
                    const Float hz = 20.0f * std::pow(1000.0f, unit);
                    m_graphX[index] = hz;
                    m_graphY[index] = AudioEffectResponseDb(m_draft, hz, GraphSampleRate);
                    m_guideX[index] = hz;
                    m_guideY[index] = 0.0f;
                }
                series[seriesCount++] = {{m_guideX, points}, {m_guideY, points}, Widget::GraphStyle::Guide};
                series[seriesCount++] = {{m_graphX, points}, {m_graphY, points}, Widget::GraphStyle::Area, eq ? Float(0.0f) : desc.y.min};
                if (eq)
                {
                    markers[markerCount++] = v[1];
                    markers[markerCount++] = v[3];
                    markers[markerCount++] = v[5];
                }
                else
                {
                    markers[markerCount++] = v[0];
                }
                break;
            }
            case AudioEffectKind::Distortion:
            {
                caption = Loc::TextOr(LocKeys::SoundEffectGraphShape, "Input → output wave");
                desc.x = {-1.0f, 1.0f, false, {UnitTicks, std::size(UnitTicks)}};
                desc.y = {-1.0f, 1.0f, false, {UnitTicks, std::size(UnitTicks)}};
                for (UInt32 index = 0; index < points; ++index)
                {
                    const Float input = -1.0f + 2.0f * static_cast<JBro::Float>(index) / static_cast<JBro::Float>(points - 1);
                    m_guideX[index] = input;
                    m_guideY[index] = input;
                    m_graphX[index] = input;
                    m_graphY[index] = AudioEffectShape(m_draft, input);
                }
                series[seriesCount++] = {{m_guideX, points}, {m_guideY, points}, Widget::GraphStyle::Guide};
                series[seriesCount++] = {{m_graphX, points}, {m_graphY, points}, Widget::GraphStyle::Line};
                break;
            }
            case AudioEffectKind::Compressor:
            case AudioEffectKind::Limiter:
            {
                caption = Loc::TextOr(LocKeys::SoundEffectGraphDynamics, "Input level → output level (dB)");
                desc.x = {-60.0f, 0.0f, false, {LevelTicks, std::size(LevelTicks)}};
                desc.y = {-60.0f, 0.0f, false, {LevelTicks, std::size(LevelTicks)}};
                for (UInt32 index = 0; index < points; ++index)
                {
                    const Float input = -60.0f + 60.0f * static_cast<JBro::Float>(index) / static_cast<JBro::Float>(points - 1);
                    m_guideX[index] = input;
                    m_guideY[index] = input;
                    m_graphX[index] = input;
                    m_graphY[index] = AudioEffectCompressedDb(m_draft, input);
                }
                series[seriesCount++] = {{m_guideX, points}, {m_guideY, points}, Widget::GraphStyle::Guide};
                series[seriesCount++] = {{m_graphX, points}, {m_graphY, points}, Widget::GraphStyle::Area, -60.0f};
                markers[markerCount++] = v[0];
                break;
            }
            case AudioEffectKind::Echo:
            {
                // 원음 하나와 되풀이들이다. 되풀이마다 `decay` 만큼 줄고(DSP 처럼 0.95 에서 자른다) 크기는 `wet` 에서 시작한다.
                caption = Loc::TextOr(LocKeys::SoundEffectGraphEcho, "Repeats (seconds)");
                // 0 초의 원음 막대가 왼쪽 끝에 반쯤 잘리지 않게 조금 앞에서 시작한다.
                desc.x = {-0.05f, 3.0f, false, {SecondsTicks, std::size(SecondsTicks)}};
                desc.y = {0.0f, 1.0f, false, {AmountTicks, std::size(AmountTicks)}};
                const Float delay = v[0] > 0.01f ? v[0] : Float(0.01f);
                const Float decay = v[1] < 0.0f ? Float(0.0f) : (v[1] > 0.95f ? Float(0.95f) : v[1]);
                m_guideX[0] = 0.0f;
                m_guideY[0] = 1.0f;
                UInt32 taps = 0;
                Float level = v[2];
                for (Float at = delay; at <= desc.x.max && taps < points && level > 0.001f; at += delay)
                {
                    m_graphX[taps] = at;
                    m_graphY[taps] = level;
                    level *= decay;
                    ++taps;
                }
                series[seriesCount++] = {{m_guideX, 1}, {m_guideY, 1}, Widget::GraphStyle::Stems};
                series[seriesCount++] = {{m_graphX, taps}, {m_graphY, taps}, Widget::GraphStyle::Stems};
                break;
            }
            case AudioEffectKind::Reverb:
            {
                // 원음(`dry`)과 잔향 꼬리(`wet` 에서 시작해 콤 되먹임으로 준다)다.
                caption = Loc::TextOr(LocKeys::SoundEffectGraphReverb, "Reverb tail (seconds)");
                desc.x = {-0.05f, 3.0f, false, {SecondsTicks, std::size(SecondsTicks)}};
                desc.y = {0.0f, 1.0f, false, {AmountTicks, std::size(AmountTicks)}};
                for (UInt32 index = 0; index < points; ++index)
                {
                    const Float at = desc.x.max * static_cast<JBro::Float>(index) / static_cast<JBro::Float>(points - 1);
                    m_graphX[index] = at;
                    m_graphY[index] = v[3] * AudioReverbTail(m_draft, at);
                }
                m_guideX[0] = 0.0f;
                m_guideY[0] = v[4];
                series[seriesCount++] = {{m_graphX, points}, {m_graphY, points}, Widget::GraphStyle::Area, 0.0f};
                series[seriesCount++] = {{m_guideX, 1}, {m_guideY, 1}, Widget::GraphStyle::Stems};
                break;
            }
            case AudioEffectKind::Chorus:
            {
                // 지연은 12 ms 에서 `depth` 만큼 `rate` 로 흔들린다. 오른쪽(흐린 선)은 1/4 주기 늦다 - DSP 와 같다.
                caption = Loc::TextOr(LocKeys::SoundEffectGraphChorus, "Delay sweep (ms)");
                desc.x = {0.0f, 2.0f, false, {SecondsTicks, 5}};
                desc.y = {0.0f, 22.0f, false, {MillisecondTicks, std::size(MillisecondTicks)}};
                constexpr Float Tau = 6.28318530717958647692f;
                for (UInt32 index = 0; index < points; ++index)
                {
                    const Float at = desc.x.max * static_cast<JBro::Float>(index) / static_cast<JBro::Float>(points - 1);
                    m_graphX[index] = at;
                    m_guideX[index] = at;
                    m_graphY[index] = 12.0f + v[2] * (0.5f + 0.5f * std::sin(Tau * v[1] * at));
                    m_guideY[index] = 12.0f + v[2] * (0.5f + 0.5f * std::sin(Tau * (v[1] * at + 0.25f)));
                }
                series[seriesCount++] = {{m_guideX, points}, {m_guideY, points}, Widget::GraphStyle::Guide};
                series[seriesCount++] = {{m_graphX, points}, {m_graphY, points}, Widget::GraphStyle::Line};
                break;
            }
            case AudioEffectKind::PitchShift:
            {
                // 원음(흐린 선) 두 주기와 같은 시간의 바뀐 소리다. 한 옥타브 올리면 주기가 두 배로 촘촘해진다.
                caption = Loc::TextOr(LocKeys::SoundEffectGraphPitch, "Pitch change (semitones)");
                desc.x = {0.0f, 1.0f, false, {}};
                desc.y = {-1.2f, 1.2f, false, {}};
                const Float ratio = std::pow(2.0f, v[0] / 12.0f);
                constexpr Float Tau = 6.28318530717958647692f;
                for (UInt32 index = 0; index < points; ++index)
                {
                    const Float at = static_cast<JBro::Float>(index) / static_cast<JBro::Float>(points - 1);
                    m_guideX[index] = at;
                    m_guideY[index] = std::sin(Tau * 2.0f * at);
                    m_graphX[index] = at;
                    m_graphY[index] = std::sin(Tau * 2.0f * ratio * at);
                }
                series[seriesCount++] = {{m_guideX, points}, {m_guideY, points}, Widget::GraphStyle::Guide};
                series[seriesCount++] = {{m_graphX, points}, {m_graphY, points}, Widget::GraphStyle::Line};
                break;
            }
            default:
                return;
        }
        desc.series = {series, seriesCount};
        desc.markers = {markers, markerCount};
        desc.height = ImGui::GetFrameHeight() * 7.0f;
        desc.title = caption;
        Widget::Graph("##response", desc);
    }

    void SoundEffectPanel::DrawParameters()
    {
        Widget::FormLayout layout("##soundEffect");
        // 파라미터는 이름 그대로 보인다(필드는 필드 이름으로). 값에는 단위가 붙고(`Hz`·`dB`·`s`), 주파수는 로그 눈금이다(D-309).
        // 슬라이더를 움직이면 미리 듣기와 그래프에 곧바로 걸린다.
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(m_draft.kind);
        for (UInt32 index = 0; index < info.paramCount; ++index)
        {
            const AudioEffectParam& param = info.params[index];
            layout.Row([&param] { Widget::Text(param.name); },
                [&] {
                    char id[48] = {};
                    std::snprintf(id, sizeof(id), "##%s", param.name);
                    Float value = m_draft.values[index];
                    Widget::SliderField slider(id, value, param.minValue, param.maxValue);
                    slider.Format(param.format != nullptr ? param.format : "%.2f");
                    if (param.logarithmic)
                    {
                        slider.Logarithmic();
                    }
                    if (slider())
                    {
                        SetParameter(index, value);
                    }
                });
        }
    }

    void SoundEffectPanel::DrawPreview()
    {
        Widget::SectionHeader(Loc::TextOr(LocKeys::SoundEffectPreview, "Preview")).SpacingBefore().Draw();
        if (m_editor->GetAudio() == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorAudioOff, "Audio is off in this editor"));
            return;
        }
        RefreshChoices();
        Widget::FormLayout layout("##soundEffectPreview");
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SoundEffectTestSound, "Test sound")),
            [&] {
                Widget::AssetField("##testSound", {m_soundNamePointers.Data(), m_soundNamePointers.Size()},
                    {m_soundIds.Data(), m_soundIds.Size()}, m_testSound)
                    .Draw();
            });
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioPreview, "Preview")),
            [&] {
                if (IsPreviewing())
                {
                    if (Widget::IconButton("##effect_stop", Icons::Stop)
                            .Caption(Loc::TextOr(LocKeys::InspectorAudioStop, "Stop"))
                            .Draw())
                    {
                        StopPreview();
                    }
                }
                else if (Widget::IconButton("##effect_play", Icons::Play)
                             .Caption(Loc::TextOr(LocKeys::InspectorAudioPlay, "Play"))
                             .Draw())
                {
                    StartPreview();
                }
            });
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioLoop, "Loop")),
            [&] { Widget::Checkbox("##loop", m_loop); });
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SoundEffectBypass, "Bypass"))
                       .Tooltip(Loc::TextOr(LocKeys::SoundEffectBypassHelp, "Play the test sound without the effect to compare")),
            [&] {
                Bool bypass = m_bypass;
                if (Widget::Checkbox("##bypass", bypass))
                {
                    SetBypass(bypass);
                }
            });
        // 울리는 동안 Master 의 크기다. 미리 듣기는 Master 로 바로 간다.
        if (IsPreviewing())
        {
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SoundEffectOutput, "Output")),
                [&] { Widget::LevelMeter("##output", m_editor->GetAudio()->GetBusPeak(AudioBusName{}), ImGui::GetFrameHeight() * 0.6f); });
        }
        if (m_testSound.IsNull())
        {
            Widget::HintText(Loc::TextOr(LocKeys::SoundEffectPreviewHint,
                "Pick an audio asset above or drag one onto it, then play"));
        }
    }

    void SoundEffectPanel::OnDraw()
    {
        if (m_asset.IsNull())
        {
            Widget::HintText(Loc::TextOr(LocKeys::SoundEffectNone, "There are no sound effects in this project"));
            return;
        }
        // 미리 듣기가 다른 쪽에서 끝났으면(인스펙터가 다른 소리를 틀었다) 이 창은 더 울리지 않는다.
        if (m_previewing && false == IsPreviewing())
        {
            m_previewing = false;
        }
        DrawKind();
        DrawGraph();
        DrawParameters();
        // 기존의 "Apply Effect" 단추다(바뀐 것이 있을 때만). 메뉴의 저장과 같은 일이다. 옆의 되돌리기는 초안만 버린다.
        if (IsDirty())
        {
            if (Widget::ActionButton(Loc::TextOr(LocKeys::SoundEffectApply, "Apply"), Widget::Severity::Success))
            {
                Save();
            }
            ImGui::SameLine();
            if (Widget::ActionButton(Loc::TextOr(LocKeys::SoundEffectRevert, "Revert")))
            {
                Revert();
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::SoundEffectRevertHelp, "Throw away the changes and go back to the saved values"));
        }
        if (false == m_message.empty())
        {
            Widget::ValidationMessage(m_messageIsError ? Widget::Severity::Error : Widget::Severity::Info, m_message.c_str()).Draw();
        }
        DrawPreview();
    }
}
