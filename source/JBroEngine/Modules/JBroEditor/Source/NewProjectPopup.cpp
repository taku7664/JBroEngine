#include "NewProjectPopup.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/TextField.h>

#include <imgui.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    namespace
    {
        // 프레임워크 이름은 번역하지 않는다 - 타입 이름과 같은 자리다(§11.2).
        const char* const FrameworkNames[] = {"2D", "3D"};
    }

    NewProjectPopup::NewProjectPopup(const char* parentFolder)
        : m_parentFolder(parentFolder != nullptr ? parentFolder : "")
    {
    }

    const char* NewProjectPopup::GetTitle() const
    {
        return Loc::TextOr(LocKeys::MenuNewProject, "New Project");
    }

    const char* NewProjectPopup::GetId() const
    {
        return "new_project";
    }

    Float NewProjectPopup::GetInitialWidth() const
    {
        return 460.0f;
    }

    void NewProjectPopup::OnDraw(EditorApplication& editor)
    {
        Bool submitted = false;
        {
            Widget::FormLayout layout("##newProject");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewProjectLocation, "Location")), [&]() {
                Widget::WrappedText(m_parentFolder.c_str());
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewProjectName, "Name"))
                    .Tooltip(Loc::TextOr(LocKeys::NewProjectNameHint, "the folder and the project file take this name"))
                    .Required(),
                [&]() {
                    // 팝업이 뜬 첫 프레임에 이름 칸으로 간다. 폴더를 고른 손이 바로 이름을 친다. 거절된 뒤에도 돌아간다.
                    if (ImGui::IsWindowAppearing() || m_refocus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_refocus = false;
                    }
                    submitted = Widget::TextField("##name", m_name).CommitOnEnter().LiveText().MaxLength(128)();
                });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewProjectFramework, "Framework")), [&]() {
                Widget::FilterCombo("##framework", ArrayView<const char* const>(FrameworkNames, 2), m_framework)
                    .ShowFilter(false)();
            });
        }
        // **치는 동안 검사한다**(새 스크립트 팝업과 같다). Enter 를 눌러야 틀렸다고 알던 때는 `a/b` 를 치고도 만들기가 켜져 있었다.
        // 빈 이름은 글로 탓하지 않고 만들기만 막는다.
        const ProjectCreateFailure nameProblem = m_name.empty() ? ProjectCreateFailure::None : editor.CheckProjectName(m_parentFolder.c_str(), m_name.c_str());
        if (nameProblem == ProjectCreateFailure::InvalidName)
        {
            Widget::ValidationMessage(Widget::Severity::Error, Loc::TextOr(LocKeys::NewProjectInvalidName, "That name cannot be a folder name"))();
        }
        else if (nameProblem == ProjectCreateFailure::AlreadyExists)
        {
            Widget::ValidationMessage(Widget::Severity::Error,
                Loc::TextOr(LocKeys::NewProjectAlreadyExists, "Something with that name is already in the folder"))();
        }
        else if (false == m_error.empty() && m_name == m_errorName)
        {
            Widget::ValidationMessage(Widget::Severity::Error, m_error.c_str())();
        }
        ImGui::Spacing();

        const Bool canCreate = false == m_name.empty() && nameProblem == ProjectCreateFailure::None;
        {
            Widget::DisableScope disabled(false == canCreate);
            if (Widget::ActionButton(Loc::TextOr(LocKeys::CommonCreate, "Create"),
                    Widget::Severity::Success))
            {
                submitted = true;
            }
        }
        ImGui::SameLine();
        if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            Close();
            return;
        }
        if (submitted && false == canCreate)
        {
            m_refocus = true;
        }
        if (submitted && canCreate)
        {
            const FrameworkKind framework = m_framework == 1 ? FrameworkKind::Framework3D : FrameworkKind::Framework2D;
            ProjectCreateFailure failure = ProjectCreateFailure::None;
            if (editor.CreateProject(m_parentFolder.c_str(), m_name.c_str(), framework, &failure))
            {
                Close();
                return;
            }
            // 까닭은 번역된 한 문장으로 보인다. 엔진이 적은 영어는 로그에 남는다.
            m_errorName = m_name;
            m_refocus = true;
            switch (failure)
            {
            case ProjectCreateFailure::InvalidName:
                m_error = Loc::TextOr(LocKeys::NewProjectInvalidName, "That name cannot be a folder name");
                break;
            case ProjectCreateFailure::AlreadyExists:
                m_error = Loc::TextOr(LocKeys::NewProjectAlreadyExists, "Something with that name is already in the folder");
                break;
            case ProjectCreateFailure::CannotWrite:
                m_error = Loc::TextOr(LocKeys::NewProjectCannotWrite, "The project files could not be written");
                break;
            default:
                m_error = Loc::TextOr(LocKeys::PopupNewProjectFailed, "The project could not be created");
                break;
            }
        }
    }
}
