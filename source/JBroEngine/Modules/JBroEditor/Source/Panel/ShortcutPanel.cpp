#include "ShortcutPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorShortcutManager.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FormLayout.h>

#include <imgui.h>

#include <cstring>

namespace JBro
{
    const char* ShortcutPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* ShortcutPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelShortcuts, "Shortcuts");
    }

    bool ShortcutPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        // 늘 보는 창이 아니다. 창 메뉴에서 열어 본다.
        SetOpen(false);
        return true;
    }

    void ShortcutPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        const EditorShortcutManager& shortcuts = m_editor->GetShortcuts();
        const std::uint32_t count = shortcuts.GetCount();

        // **무리는 처음 나온 차례대로 모은다.** 전역 것이 먼저 등록되고 패널 것이 나중에 온다 - 등록 차례로만 그리면
        // 같은 무리가 두 번 나뉘어 나온다. 목록은 열두어 줄이라 두 겹으로 돌아도 가볍다.
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
            if (head != 0)
            {
                ImGui::Spacing();
            }
            Widget::SectionHeader(Loc::TextOr(first.categoryKey, first.categoryKey)).Draw();
            for (std::uint32_t index = head; index < count; ++index)
            {
                const EditorShortcutView info = shortcuts.GetAt(index);
                if (std::strcmp(info.categoryKey, first.categoryKey) != 0)
                {
                    continue;
                }
                // 라벨과 조합키를 두 칸으로 나눈다(§11.3). 붙여 쓰면 키가 어디서 시작하는지
                // 줄마다 달라 읽히지 않는다.
                Widget::FormLayout layout("##shortcut");
                layout.Row(
                    [&] { Widget::Text(Loc::TextOr(info.labelKey, info.labelKey)); },
                    [&]
                    {
                        const EditorShortcutText primary = EditorShortcutManager::Describe(info.primary);
                        const EditorShortcutText secondary = EditorShortcutManager::Describe(info.secondary);
                        if (secondary.value[0] != '\0')
                        {
                            Widget::TextF("%s, %s", primary.value, secondary.value);
                        }
                        else
                        {
                            Widget::Text(primary.value);
                        }
                    });
            }
        }
    }
}
