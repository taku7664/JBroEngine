#include "ShortcutPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorShortcuts.h>
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
        return "Shortcuts";
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
        const JArrayView<EditorShortcutInfo> all = EditorShortcuts::All();

        // 무리는 표에 적힌 차례대로 나온다. 무리 이름을 따로 모아 두지 않는 이유는
        // 표가 이미 무리별로 모여 있기 때문이다 - 두 곳에 적으면 어긋난다.
        const char* drawnCategory = nullptr;
        for (std::uint32_t index = 0; index < all.size; ++index)
        {
            const EditorShortcutInfo& info = all.data[index];
            const char* category = Loc::TextOr(info.categoryKey, info.categoryKey);
            if (drawnCategory == nullptr || std::strcmp(drawnCategory, category) != 0)
            {
                if (drawnCategory != nullptr)
                {
                    ImGui::Spacing();
                }
                Widget::SectionHeader(category).Draw();
                drawnCategory = category;
            }

            // 라벨과 조합키를 두 칸으로 나눈다(§11.3). 붙여 쓰면 키가 어디서 시작하는지
            // 줄마다 달라 읽히지 않는다.
            Widget::FormLayout layout("##shortcut");
            layout.Row(
                [&] { Widget::Text(Loc::TextOr(info.labelKey, info.labelKey)); },
                [&]
                {
                    const EditorShortcutText primary = EditorShortcuts::Describe(info.primary);
                    const EditorShortcutText secondary = EditorShortcuts::Describe(info.secondary);
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
