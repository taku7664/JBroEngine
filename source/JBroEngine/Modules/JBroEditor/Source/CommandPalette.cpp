#include <JBro/Editor/CommandPalette.h>

#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/GuideFocus.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        Bool OpenPalette(EditorActionContext& context)
        {
            return context.editor->OpenPopup(MakeOwnerPtr<CommandPalettePopup>()) != InvalidPopupHandle;
        }

        const char* Translate(const char* key)
        {
            return key != nullptr ? Loc::Text(key) : "";
        }
    }

    const char* CommandPalettePopup::GetTitle() const
    {
        return Loc::TextOr(LocKeys::CommandPaletteTitle, "Command Palette");
    }

    const char* CommandPalettePopup::GetId() const
    {
        return PopupId;
    }

    Float CommandPalettePopup::GetInitialWidth() const
    {
        return 520.0f;
    }

    Float CommandPalettePopup::GetInitialHeight() const
    {
        return 420.0f;
    }

    void CommandPalettePopup::Collect(const EditorApplication& editor, const char* query, Array<const EditorActionInfo*>& out)
    {
        out.Clear();
        const EditorActionRegistry& actions = EditorActionRegistry::Get();
        for (UInt32 index = 0; index < actions.GetCount(); ++index)
        {
            const EditorActionInfo& action = actions.GetAt(index);
            if (action.componentType != InvalidComponentTypeId || std::strcmp(action.name, CommandPalettePopup::PopupId) == 0)
            {
                continue;
            }
            // 단축키 설정 화면과 같은 찾기다 - 번역된 이름·무리·저장 이름·지금 조합 글자.
            const EditorShortcutView view = editor.GetShortcuts().Find(action.name);
            if (EditorShortcutManager::MatchesSearch(query, EditorActionUi::Label(action, EditorActionMenu::None),
                    Translate(action.categoryKey), view))
            {
                out.Add(&action);
            }
        }
    }

    void CommandPalettePopup::OnDraw(EditorApplication& editor)
    {
        // 열자마자 찾기 칸에 글자가 들어가야 한다. 마우스로 칸을 다시 누르게 하면 팔레트를 연 까닭이 없다.
        if (m_focusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        Widget::SearchBox("##command_search", m_search)
            .Hint(Loc::TextOr(LocKeys::CommandPaletteSearchHint, "Search by name or key"))
            .Width(ImGui::GetContentRegionAvail().x)
            .Draw();
        // Esc 와 Enter 는 칸 안의 편집 키다 - 단축키가 아니다(D-228 의 `FilterCombo` 의 Enter 와 같다).
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            Close();
            return;
        }
        Collect(editor, m_search.c_str(), m_matches);

        EditorActionContext context;
        context.editor = &editor;
        const EditorActionInfo* chosen = nullptr;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        {
            for (const EditorActionInfo* action : m_matches)
            {
                if (EditorActionUi::CanExecute(*action, context))
                {
                    chosen = action;
                    break;
                }
            }
        }

        ImGui::Spacing();
        if (m_matches.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::CommandPaletteNoMatch, "No commands match"));
        }
        for (const EditorActionInfo* action : m_matches)
        {
            // `무리: 이름` 으로 보인다. 같은 이름이 무리마다 있을 수 있다(편집의 붙여넣기와 다른 것).
            char label[160] = {};
            std::snprintf(label, sizeof(label), "%s: %s", Translate(action->categoryKey),
                EditorActionUi::Label(*action, EditorActionMenu::None));
            const Bool enabled = EditorActionUi::CanExecute(*action, context);
            const EditorShortcutText keys = EditorActionUi::Keys(editor, *action);
            Widget::SetNextItemTarget(GuideFocusTargets::Action(action->name));
            if (Widget::MenuItem(label, keys.value, enabled, enabled ? nullptr : EditorActionUi::WhyBlocked(*action, context),
                    action->icon))
            {
                chosen = action;
            }
        }
        if (chosen != nullptr)
        {
            // 먼저 닫는다. 행동이 다른 팝업을 열거나 팔레트가 가리던 것을 바꿔도 팔레트가 남지 않게.
            Close();
            EditorActionUi::Execute(*chosen, context);
        }
    }

    namespace CommandPalette
    {
        void RegisterAction()
        {
            EditorActionInfo info;
            info.name = CommandPalettePopup::PopupId;
            info.labelKey = LocKeys::CommandPaletteTitle;
            info.fallbackLabel = "Command Palette";
            info.categoryKey = LocKeys::MenuHelp;
            // VSCode 와 같은 조합이다. 쓰던 사람이 손으로 아는 값이다.
            info.primary = EditorShortcutBinding{ImGuiKey_P, true, true, false};
            info.placedByEditor = true;
            info.Execute = &OpenPalette;
            EditorActionRegistry::Get().Register(info);
        }
    }
}
