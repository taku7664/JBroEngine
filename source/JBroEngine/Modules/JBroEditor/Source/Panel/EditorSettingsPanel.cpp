#include "EditorSettingsPanel.h"

#include <JBro/Editor/ConfirmPopup.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace JBro
{
    namespace
    {
        constexpr float PageListWidth = 150.0f;
        // 이름 칸과 조합 칸의 가장 넓은 폭과 가장 좁은 폭. 창이 좁으면 줄어든다 - 고정해 두면 좁은 도크에서 기본값 단추가 창 밖으로 밀린다.
        constexpr float MaxLabelWidth = 190.0f;
        constexpr float MinLabelWidth = 90.0f;
        constexpr float MaxBindingWidth = 150.0f;
        constexpr float MinBindingWidth = 80.0f;

        float Clamp(float value, float low, float high)
        {
            return value < low ? low : (value > high ? high : value);
        }

        const char* Translate(const char* key)
        {
            return Loc::TextOr(key, key);
        }
    }

    const char* EditorSettingsPanel::GetTitle() const
    {
        // 안정된 이름이다. 번역하지 않는다.
        return TypeName;
    }

    const char* EditorSettingsPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelEditorSettings, "Editor Settings");
    }

    bool EditorSettingsPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        SetOpen(false);
        return true;
    }

    void EditorSettingsPanel::OnDestroy()
    {
        StopCapture();
    }

    void EditorSettingsPanel::OnUpdate(float deltaTime)
    {
        (void)deltaTime;
        // 닫히거나 가려져 그리지 않는 창이 키를 붙잡고 있으면 에디터의 모든 단축키가 멈춘 채로 남는다.
        if (false == m_captureId.IsEmpty() && false == IsOpen())
        {
            StopCapture();
        }
    }

    void EditorSettingsPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        if (ImGui::BeginChild("##settings_pages", ImVec2(PageListWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            if (Widget::SelectableRow(Loc::TextOr(LocKeys::EditorSettingsShortcuts, "Shortcuts"), m_page == Page::Shortcuts))
            {
                m_page = Page::Shortcuts;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##settings_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            if (m_page == Page::Shortcuts)
            {
                DrawShortcuts();
            }
        }
        ImGui::EndChild();
    }

    void EditorSettingsPanel::DrawShortcuts()
    {
        UpdateCapture();
        EditorShortcutManager& shortcuts = m_editor->GetShortcuts();

        // 검색 칸은 `모두 기본값으로` 단추를 뺀 남은 폭을 다 쓴다. (`SearchBox` 는 음수 폭을 "남은 폭 빼기" 로 읽지 않는다 - 처음에
        // 음수를 줬다가 칸이 1 픽셀이 되었다.)
        const char* resetAllLabel = Loc::TextOr(LocKeys::EditorSettingsResetAll, "Reset All");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float resetAllWidth = ImGui::CalcTextSize(resetAllLabel).x + style.FramePadding.x * 2.0f;
        const float searchWidth = ImGui::GetContentRegionAvail().x - resetAllWidth - style.ItemSpacing.x;
        Widget::SearchBox("##shortcut_search", m_search)
            .Hint(Loc::TextOr(LocKeys::EditorSettingsSearchHint, "Search by name or key"))
            .Width(searchWidth > 60.0f ? searchWidth : 60.0f)
            .Draw();
        ImGui::SameLine();
        if (Widget::ActionButton(resetAllLabel, Widget::Severity::Warning))
        {
            m_editor->OpenPopup(MakeOwnerPtr<ConfirmPopup>(
                Loc::TextOr(LocKeys::EditorSettingsResetAllTitle, "Reset Shortcuts"),
                Loc::TextOr(LocKeys::EditorSettingsResetAllMessage, "Every shortcut goes back to its default keys."),
                Loc::TextOr(LocKeys::EditorSettingsResetAllConfirm, "Reset"), nullptr,
                Loc::TextOr(LocKeys::CommonCancel, "Cancel"), &EditorSettingsPanel::AnswerResetAll, this,
                "editor_settings.reset_all"));
        }
        ImGui::Spacing();

        shortcuts.FindConflicts(m_conflicts);
        const std::uint32_t count = shortcuts.GetCount();
        bool any = false;
        // 무리는 처음 나온 차례대로 모은다(도움말 창과 같은 수). 검색에 걸린 줄이 없는 무리는 제목도 그리지 않는다.
        for (std::uint32_t head = 0; head < count; ++head)
        {
            const EditorShortcutView first = shortcuts.GetAt(head);
            bool seen = false;
            for (std::uint32_t before = 0; before < head && false == seen; ++before)
            {
                seen = std::strcmp(shortcuts.GetAt(before).categoryKey, first.categoryKey) == 0;
            }
            if (seen)
            {
                continue;
            }
            const char* category = Translate(first.categoryKey);
            bool headerDrawn = false;
            for (std::uint32_t index = head; index < count; ++index)
            {
                const EditorShortcutView view = shortcuts.GetAt(index);
                if (std::strcmp(view.categoryKey, first.categoryKey) != 0
                    || false == EditorShortcutManager::MatchesSearch(m_search.c_str(), Translate(view.labelKey), category, view))
                {
                    continue;
                }
                if (false == headerDrawn)
                {
                    if (any)
                    {
                        ImGui::Spacing();
                    }
                    Widget::SectionHeader(category).Draw();
                    headerDrawn = true;
                    any = true;
                }
                DrawShortcutRow(index, view);
            }
        }
        if (false == any)
        {
            Widget::HintText(Loc::TextOr(LocKeys::EditorSettingsNoMatch, "No shortcuts match"));
        }
    }

    void EditorSettingsPanel::DrawShortcutRow(std::uint32_t index, const EditorShortcutView& view)
    {
        EditorShortcutManager& shortcuts = m_editor->GetShortcuts();
        // 이름 칸은 폭을 고정하고 단추들을 그 뒤에 잇는다. 표(`FormLayout`)로 두면 단추의 Id 가 표의 Id 에 묶여 줄마다 달라진다 -
        // 이름(`view.id`)만으로 단추를 가리킬 수 있게 창 → 이름 → 자리 순으로 둔다.
        // 줄의 폭을 나눈다: 이름 칸 · 조합 칸 둘 · 기본값 단추 · 겹침 표시.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float available = ImGui::GetContentRegionAvail().x;
        const float resetWidth = ImGui::CalcTextSize(Loc::TextOr(LocKeys::EditorSettingsReset, "Default")).x + style.FramePadding.x * 2.0f;
        const float markWidth = ImGui::GetFontSize();
        const float labelWidth = Clamp(available * 0.35f, MinLabelWidth, MaxLabelWidth);
        const float bindingWidth = Clamp(
            (available - labelWidth - resetWidth - markWidth - style.ItemSpacing.x * 4.0f) * 0.5f, MinBindingWidth, MaxBindingWidth);
        ImGui::PushID(view.id);
        {
            Widget::Text(Translate(view.labelKey));
            ImGui::SameLine(labelWidth);
            {
                const EditorShortcutBinding slots[2] = {view.primary, view.secondary};
                for (std::uint32_t slot = 0; slot < 2; ++slot)
                {
                    if (slot != 0)
                    {
                        ImGui::SameLine();
                    }
                    const bool capturing = m_captureId == view.id && m_captureSlot == slot;
                    const EditorShortcutText text = EditorShortcutManager::Describe(slots[slot]);
                    const char* shown = capturing ? Loc::TextOr(LocKeys::EditorSettingsPressKey, "Press a key")
                        : (text.value[0] != '\0' ? text.value : Loc::TextOr(LocKeys::EditorSettingsEmptyBinding, "(none)"));
                    // `###` 뒤만 Id 가 된다 - 글자가 바뀌어도(키를 누르세요 → Ctrl+U) 같은 단추다.
                    char label[96] = {};
                    std::snprintf(label, sizeof(label), "%s###slot%u", shown, slot);
                    if (Widget::ActionButton(label, capturing ? Widget::Severity::Success : Widget::Severity::Info, true, nullptr,
                            ImVec2(bindingWidth, 0.0f)))
                    {
                        if (capturing)
                        {
                            StopCapture();
                        }
                        else
                        {
                            StartCapture(view.id, slot);
                        }
                    }
                    if (capturing)
                    {
                        Widget::HoveredTooltip(Loc::TextOr(LocKeys::EditorSettingsCaptureHint, "Press Esc to cancel"));
                    }
                }
                if (m_captureId == view.id)
                {
                    ImGui::SameLine();
                    char clearLabel[64] = {};
                    std::snprintf(clearLabel, sizeof(clearLabel), "%s###clear", Loc::TextOr(LocKeys::EditorSettingsClearBinding, "Clear"));
                    if (Widget::Button(clearLabel))
                    {
                        shortcuts.SetBinding(view.id, m_captureSlot, EditorShortcutBinding{});
                        StopCapture();
                    }
                }
                ImGui::SameLine();
                char resetLabel[64] = {};
                std::snprintf(resetLabel, sizeof(resetLabel), "%s###reset", Loc::TextOr(LocKeys::EditorSettingsReset, "Default"));
                if (Widget::ActionButton(resetLabel, Widget::Severity::Info,
                        view.customized, Loc::TextOr(LocKeys::EditorSettingsResetTooltip, "Restore the default keys")))
                {
                    shortcuts.ResetBinding(view.id);
                }
                Widget::HoveredTooltip(Loc::TextOr(LocKeys::EditorSettingsResetTooltip, "Restore the default keys"));

                // 겹침: 이 줄이 낀 겹침 가운데 가장 무거운 것 하나를 표시하고, 상대의 이름을 툴팁으로.
                for (const ShortcutConflict& conflict : m_conflicts)
                {
                    if (conflict.first != index && conflict.second != index)
                    {
                        continue;
                    }
                    const std::uint32_t other = conflict.first == index ? conflict.second : conflict.first;
                    const char* otherLabel = Translate(shortcuts.GetAt(other).labelKey);
                    char reason[256] = {};
                    Widget::Severity severity = Widget::Severity::Warning;
                    if (conflict.kind == ShortcutConflictKind::Clash)
                    {
                        severity = Widget::Severity::Error;
                        std::snprintf(reason, sizeof(reason), Loc::TextOr(LocKeys::EditorSettingsClash, "Another shortcut uses the same keys: %s"), otherLabel);
                    }
                    else if (conflict.first == index)
                    {
                        std::snprintf(reason, sizeof(reason), Loc::TextOr(LocKeys::EditorSettingsShadows, "In this panel this shortcut runs instead of: %s"), otherLabel);
                    }
                    else
                    {
                        std::snprintf(reason, sizeof(reason), Loc::TextOr(LocKeys::EditorSettingsShadowed, "A focused panel runs its own shortcut instead: %s"), otherLabel);
                    }
                    ImGui::SameLine();
                    Widget::SeverityTextF(severity, "%s", "!");
                    Widget::HoveredTooltip(reason);
                    break;
                }
            }
        }
        ImGui::PopID();
    }

    void EditorSettingsPanel::UpdateCapture()
    {
        if (m_captureId.IsEmpty())
        {
            return;
        }
        EditorShortcutBinding pressed;
        if (false == EditorShortcutManager::CaptureBinding(pressed))
        {
            return;
        }
        // 조합키 없는 Esc 는 취소다. Ctrl+Esc 같은 것은 조합으로 받는다.
        if (pressed.key == ImGuiKey_Escape && false == pressed.control && false == pressed.shift && false == pressed.alt)
        {
            StopCapture();
            return;
        }
        m_editor->GetShortcuts().SetBinding(m_captureId.c_str(), m_captureSlot, pressed);
        StopCapture();
    }

    void EditorSettingsPanel::StartCapture(const char* id, std::uint32_t slot)
    {
        m_captureId = id;
        m_captureSlot = slot;
        // 잡는 동안은 에디터의 단축키가 돌지 않는다 - 새 조합으로 누른 Ctrl+S 가 저장하면 안 된다.
        m_editor->GetShortcuts().SetSuspended(true);
    }

    void EditorSettingsPanel::StopCapture()
    {
        if (m_captureId.IsEmpty())
        {
            return;
        }
        m_captureId.clear();
        if (m_editor != nullptr)
        {
            m_editor->GetShortcuts().SetSuspended(false);
        }
    }

    void EditorSettingsPanel::AnswerResetAll(EditorApplication& editor, int choice, void* user)
    {
        (void)user;
        if (choice == 0)
        {
            editor.GetShortcuts().ResetAll();
        }
    }
}
