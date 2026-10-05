#include <JBro/Editor/MessagePopup.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui.h>

namespace JBro
{
    MessagePopup::MessagePopup(const char* title, const char* message, const char* id)
        : m_title(title != nullptr ? title : "")
        , m_message(message != nullptr ? message : "")
        , m_id(id != nullptr ? id : "")
    {
    }

    const char* MessagePopup::GetTitle() const
    {
        return m_title.c_str();
    }

    const char* MessagePopup::GetId() const
    {
        return m_id.empty() ? nullptr : m_id.c_str();
    }

    void MessagePopup::OnDraw(EditorApplication& editor)
    {
        (void)editor;
        // 글 앞에 알림 아이콘(D-278). 글이 여러 줄이어도 아이콘 뒤에서 이어진다.
        Widget::InlineIcon(Icons::Info);
        ImGui::BeginGroup();
        Widget::WrappedText(m_message.c_str());
        ImGui::EndGroup();
        ImGui::Spacing();
        if (Widget::ActionButton(Loc::TextOr(LocKeys::CommonOk, "OK"), Widget::Severity::Info,
                true, nullptr, ImVec2(96.0f, 0.0f)))
        {
            Close();
        }
    }
}
