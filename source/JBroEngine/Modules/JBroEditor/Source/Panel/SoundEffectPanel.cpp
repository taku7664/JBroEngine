#include "SoundEffectPanel.h"

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Audio/AudioSystem.h>
#include <JBro/Editor/ConfirmPopup.h>
#include <JBro/Editor/EditorActionRegistry.h>
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
    namespace
    {
        // 초안의 전후 값을 떠 둔 커맨드다. 대상은 같은 창이고, 스택이 창 안에 있어 커맨드가 창보다 오래 살지 않는다.
        class SoundEffectEditCommand final : public EditorCommand
        {
        public:
            SoundEffectEditCommand(SoundEffectPanel& panel, const char* name, const AudioEffectSettings& before,
                const AudioEffectSettings& after, const char* mergeKey)
                : m_panel(panel), m_name(name), m_before(before), m_after(after), m_mergeKey(mergeKey == nullptr ? "" : mergeKey)
            {
            }

            const char* GetName() const override
            {
                return m_name;
            }

            Bool Execute() override
            {
                m_panel.ApplyDraft(m_after);
                return true;
            }

            void Undo() override
            {
                m_panel.ApplyDraft(m_before);
            }

            void Redo() override
            {
                m_panel.ApplyDraft(m_after);
            }

            // 같은 슬라이더를 끄는 동안은 커맨드 하나다(매니저가 끌기가 이어지는지 본다).
            Bool CanMerge(const EditorCommand& newer) const override
            {
                const auto* other = dynamic_cast<const SoundEffectEditCommand*>(&newer);
                return other != nullptr && &other->m_panel == &m_panel && !m_mergeKey.empty() && other->m_mergeKey == m_mergeKey;
            }

            Bool TryMerge(const EditorCommand& newer) override
            {
                const auto* other = dynamic_cast<const SoundEffectEditCommand*>(&newer);
                if (other == nullptr)
                {
                    return false;
                }
                m_after = other->m_after;
                return true;
            }

        private:
            SoundEffectPanel& m_panel;
            const char* m_name;
            AudioEffectSettings m_before;
            AudioEffectSettings m_after;
            String m_mergeKey;
        };

        SoundEffectPanel* PanelOf(const EditorActionContext& context)
        {
            return static_cast<SoundEffectPanel*>(context.panel);
        }

        Bool CanUndo(const EditorActionContext& context)
        {
            return context.panel != nullptr && PanelOf(context)->GetCommands().CanUndo();
        }

        Bool CanRedo(const EditorActionContext& context)
        {
            return context.panel != nullptr && PanelOf(context)->GetCommands().CanRedo();
        }

        Bool CanSave(const EditorActionContext& context)
        {
            return context.panel != nullptr && PanelOf(context)->IsDirty();
        }

        const char* WhyNothing(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::SoundEffectNothingToSave, "nothing has changed");
        }
    }

    void SoundEffectPanel::RegisterActions()
    {
        EditorActionRegistry& actions = EditorActionRegistry::Get();
        struct Row
        {
            const char* name;
            const char* labelKey;
            const char* fallback;
            EditorShortcutBinding primary;
            EditorShortcutBinding secondary;
            Bool (*canExecute)(const EditorActionContext&);
            Bool (*execute)(EditorActionContext&);
        };
        auto bind = [](ImGuiKey key, Bool control = false, Bool shift = false)
        {
            EditorShortcutBinding binding;
            binding.key = key;
            binding.control = control;
            binding.shift = shift;
            return binding;
        };
        const Row rows[] = {
            {"sound_effect.undo", LocKeys::MenuUndo, "Undo", bind(ImGuiKey_Z, true), {}, &CanUndo,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Undo(); }},
            {"sound_effect.redo", LocKeys::MenuRedo, "Redo", bind(ImGuiKey_Y, true), bind(ImGuiKey_Z, true, true), &CanRedo,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Redo(); }},
            {"sound_effect.save", LocKeys::SoundEffectSave, "Save", bind(ImGuiKey_S, true), {}, &CanSave,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Save(); }},
        };
        for (const Row& row : rows)
        {
            EditorActionInfo info;
            info.name = row.name;
            info.labelKey = row.labelKey;
            info.fallbackLabel = row.fallback;
            info.categoryKey = LocKeys::SoundEffectTitle;
            info.panelType = TypeName;
            info.primary = row.primary;
            info.secondary = row.secondary;
            info.CanExecute = row.canExecute;
            info.WhyBlocked = &WhyNothing;
            info.Execute = row.execute;
            actions.Register(info);
        }
    }

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
        m_commands.Clear();
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
        // 다시 읽은 파일은 새 출발이다 - 옛 초안으로 되돌리는 기록은 남기지 않는다.
        m_commands.Clear();
        m_message.clear();
        UpdateTitle();
        PushDraftToPreview();
        return true;
    }

    void SoundEffectPanel::ApplyDraft(const AudioEffectSettings& settings)
    {
        m_draft = settings;
        UpdateTitle();
        PushDraftToPreview();
    }

    void SoundEffectPanel::CommitDraft(const char* name, const AudioEffectSettings& edited, const char* mergeKey)
    {
        Bool changed = edited.kind != m_draft.kind;
        for (UInt32 index = 0; index < AudioEffectMaxParams && !changed; ++index)
        {
            changed = edited.values[index] != m_draft.values[index];
        }
        if (!changed)
        {
            return;
        }
        m_commands.Execute(MakeOwnerPtr<SoundEffectEditCommand>(*this, name, m_draft, edited, mergeKey));
    }

    Bool SoundEffectPanel::Undo()
    {
        return m_commands.Undo();
    }

    Bool SoundEffectPanel::Redo()
    {
        return m_commands.Redo();
    }

    Bool SoundEffectPanel::HasUnsavedChanges() const
    {
        return IsDirty();
    }

    Bool SoundEffectPanel::SaveChanges()
    {
        return !IsDirty() || Save();
    }

    Bool SoundEffectPanel::OnCloseRequested()
    {
        if (!IsDirty())
        {
            return true;
        }
        RequestClose();
        return false;
    }

    void SoundEffectPanel::RequestReload()
    {
        if (!IsDirty())
        {
            ReloadFromDisk();
            return;
        }
        // 다시 읽으면 저장하지 않은 것이 사라진다 - 먼저 묻는다(노드 그래프 창과 같은 물음, D-309).
        m_askingReload = true;
        m_editor->OpenPopup(MakeOwnerPtr<ConfirmPopup>(
            Loc::TextOr(LocKeys::FileWindowUnsavedTitle, "Unsaved changes"),
            Loc::TextOr(LocKeys::FileWindowReloadMessage, "There are changes that are not saved. Reloading from disk throws them away"),
            nullptr,
            Loc::TextOr(LocKeys::FileWindowReloadDiscard, "Reload without saving"),
            Loc::TextOr(LocKeys::CommonCancel, "Cancel"),
            &SoundEffectPanel::AnswerReload, nullptr, "##SoundEffectReload"));
    }

    void SoundEffectPanel::RequestClose()
    {
        if (!IsDirty())
        {
            m_editor->ClosePanel(*this);
            return;
        }
        m_askingClose = true;
        m_editor->OpenPopup(MakeOwnerPtr<ConfirmPopup>(
            Loc::TextOr(LocKeys::FileWindowUnsavedTitle, "Unsaved changes"),
            Loc::TextOr(LocKeys::FileWindowCloseMessage, "There are changes that are not saved"),
            Loc::TextOr(LocKeys::FileWindowSaveAndClose, "Save and close"),
            Loc::TextOr(LocKeys::FileWindowCloseWithoutSaving, "Close without saving"),
            Loc::TextOr(LocKeys::CommonCancel, "Cancel"),
            &SoundEffectPanel::AnswerClose, nullptr, "##SoundEffectClose"));
    }

    void SoundEffectPanel::AnswerReload(EditorApplication& editor, Int32 choice, void*)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(TypeName, panels);
        for (EditorPanel* panel : panels)
        {
            auto* effect = static_cast<SoundEffectPanel*>(panel);
            if (!effect->m_askingReload)
            {
                continue;
            }
            effect->m_askingReload = false;
            if (choice == 1)
            {
                effect->ReloadFromDisk();
            }
        }
    }

    void SoundEffectPanel::AnswerClose(EditorApplication& editor, Int32 choice, void*)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(TypeName, panels);
        for (EditorPanel* panel : panels)
        {
            auto* effect = static_cast<SoundEffectPanel*>(panel);
            if (!effect->m_askingClose)
            {
                continue;
            }
            effect->m_askingClose = false;
            // 저장하지 못했으면 닫지 않는다 - 닫으면 고친 것이 사라진다.
            if ((choice == 0 && effect->Save()) || choice == 1)
            {
                editor.ClosePanel(*effect);
            }
        }
    }

    void SoundEffectPanel::SetKind(AudioEffectKind kind)
    {
        CommitDraft("Change Effect Kind", ChangeAudioEffectKind(m_draft, kind), nullptr);
    }

    void SoundEffectPanel::SetParameter(UInt32 index, Float value)
    {
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(m_draft.kind);
        if (index >= info.paramCount)
        {
            return;
        }
        const AudioEffectParam& param = info.params[index];
        AudioEffectSettings edited = m_draft;
        edited.values[index] = value < param.minValue ? param.minValue : (value > param.maxValue ? param.maxValue : value);
        char key[32];
        std::snprintf(key, sizeof(key), "param:%u", index.Get());
        CommitDraft("Edit Effect Parameter", edited, key);
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
            RequestReload();
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::SoundEffectClose, "Close")))
        {
            RequestClose();
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
