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

    float NewProjectPopup::GetInitialWidth() const
    {
        return 460.0f;
    }

    void NewProjectPopup::OnDraw(EditorApplication& editor)
    {
        bool submitted = false;
        {
            Widget::FormLayout layout("##newProject");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewProjectLocation, "Location")), [&]() {
                Widget::WrappedText(m_parentFolder.c_str());
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewProjectName, "Name"))
                    .Tooltip(Loc::TextOr(LocKeys::NewProjectNameHint, "the folder and the project file take this name"))
                    .Required(),
                [&]() {
                    // 팝업이 뜬 첫 프레임에 이름 칸으로 간다. 폴더를 고른 손이 바로 이름을 친다.
                    if (ImGui::IsWindowAppearing())
                    {
                        ImGui::SetKeyboardFocusHere();
                    }
                    submitted = Widget::TextField("##name", m_name).CommitOnEnter().MaxLength(128)();
                });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewProjectFramework, "Framework")), [&]() {
                Widget::FilterCombo("##framework", ArrayView<const char* const>(FrameworkNames, 2), m_framework)
                    .ShowFilter(false)();
            });
        }
        if (false == m_error.empty())
        {
            Widget::ValidationMessage(Widget::Severity::Error, m_error.c_str())();
        }
        ImGui::Spacing();

        const bool canCreate = false == m_name.empty();
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
