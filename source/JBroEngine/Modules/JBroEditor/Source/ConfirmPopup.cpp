#include <JBro/Editor/ConfirmPopup.h>

#include <JBro/Editor/Widget/Basic.h>

#include <imgui.h>

namespace JBro
{
    ConfirmPopup::ConfirmPopup(const char* title, const char* message,
        const char* first, const char* second, const char* third,
        Answer answer, void* user, const char* id)
        : m_title(title != nullptr ? title : "")
        , m_message(message != nullptr ? message : "")
        , m_id(id != nullptr ? id : "")
        , m_answer(answer)
        , m_user(user)
    {
        m_labels[0] = first != nullptr ? first : "";
        m_labels[1] = second != nullptr ? second : "";
        m_labels[2] = third != nullptr ? third : "";
    }

    const char* ConfirmPopup::GetTitle() const
    {
        return m_title.c_str();
    }

    const char* ConfirmPopup::GetId() const
    {
        return m_id.empty() ? nullptr : m_id.c_str();
    }

    void ConfirmPopup::Choose(EditorApplication& editor, int choice)
    {
        // **한 번만 답한다.** 단추를 누른 프레임에 닫히지만, 그 프레임의 남은 단추들도
        // 그려지므로 두 번 답할 길이 생긴다.
        if (m_answered)
        {
            return;
        }
        m_answered = true;
        Close();
        if (m_answer != nullptr)
        {
            m_answer(editor, choice, m_user);
        }
    }

    void ConfirmPopup::OnDraw(EditorApplication& editor)
    {
        ImGui::TextWrapped("%s", m_message.c_str());
        ImGui::Spacing();
        for (int index = 0; index < 3; ++index)
        {
            if (m_labels[index].empty())
            {
                continue;
            }
            if (index != 0)
            {
                ImGui::SameLine(0.0f, 8.0f);
            }
            if (Widget::Button(m_labels[index].c_str()))
            {
                Choose(editor, index);
            }
        }
    }

    void ConfirmPopup::OnExit(EditorApplication& editor)
    {
        // 제목줄의 X 나 Esc 로 닫혔다. **고르지 않은 것은 그만두기다** - 그때도 부르는 쪽이
        // 알아야 한다. 기다리던 일을 지우지 않으면 다음에 열 때 옛 요청이 되살아난다.
        Choose(editor, Cancelled);
    }
}
