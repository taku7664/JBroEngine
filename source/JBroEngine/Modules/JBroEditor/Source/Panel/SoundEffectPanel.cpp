#include "SoundEffectPanel.h"

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Audio/AudioSystem.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/AssetField.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/Scalar.h>

#include <imgui.h>

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
        m_previewing = audio->PlayPreview(held, m_loop, &m_draft, 1);
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
            m_editor->GetAudio()->SetPreviewEffects(&m_draft, 1);
        }
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

    void SoundEffectPanel::DrawParameters()
    {
        Widget::FormLayout layout("##soundEffect");
        layout.Row([] { Widget::Text("Kind"); },
            [&] {
                const char* names[static_cast<std::size_t>(AudioEffectKind::Count)] = {};
                for (std::size_t index = 0; index < static_cast<std::size_t>(AudioEffectKind::Count); ++index)
                {
                    names[index] = AudioEffectKinds[index].name;
                }
                Int32 current = static_cast<JBro::Int32>(static_cast<std::uint8_t>(m_draft.kind));
                if (Widget::FilterCombo("##kind", {names, static_cast<std::size_t>(AudioEffectKind::Count)}, current)
                        .Draw()
                    && current >= 0)
                {
                    SetKind(static_cast<AudioEffectKind>(current.Get()));
                }
                Widget::HoveredTooltip(Loc::TextOr(LocKeys::SoundEffectKindHelp,
                    "Changing the kind keeps the values of the parameters both kinds have"));
            });
        // 파라미터는 이름 그대로 보인다(필드는 필드 이름으로). 슬라이더를 움직이면 미리 듣기에 곧바로 걸린다.
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(m_draft.kind);
        for (UInt32 index = 0; index < info.paramCount; ++index)
        {
            const AudioEffectParam& param = info.params[index];
            layout.Row([&param] { Widget::Text(param.name); },
                [&] {
                    char id[48] = {};
                    std::snprintf(id, sizeof(id), "##%s", param.name);
                    Float value = m_draft.values[index];
                    if (Widget::SliderField(id, value, param.minValue, param.maxValue)())
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
        DrawParameters();
        // 기존의 "Apply Effect" 단추다(바뀐 것이 있을 때만). 메뉴의 저장과 같은 일이다.
        if (IsDirty())
        {
            if (Widget::ActionButton(Loc::TextOr(LocKeys::SoundEffectApply, "Apply"), Widget::Severity::Success))
            {
                Save();
            }
        }
        if (false == m_message.empty())
        {
            Widget::ValidationMessage(m_messageIsError ? Widget::Severity::Error : Widget::Severity::Info, m_message.c_str()).Draw();
        }
        DrawPreview();
    }
}
