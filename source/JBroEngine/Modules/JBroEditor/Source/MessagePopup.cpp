#include <JBro/Editor/MessagePopup.h>

#include <JBro/Editor/Localization.h>
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
        ImGui::TextWrapped("%s", m_message.c_str());
        ImGui::Spacing();
        if (ImGui::Button(Loc::TextOr(LocKeys::CommonOk, "OK"), ImVec2(96.0f, 0.0f)))
        {
            Close();
        }
    }
}
