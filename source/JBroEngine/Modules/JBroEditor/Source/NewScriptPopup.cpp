#include "NewScriptPopup.h"

#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/Widget/TextField.h>

#include <imgui.h>

namespace JBro
{
    namespace
    {
        // 필드 타입 이름은 번역하지 않는다 - 헤더에 그대로 적히는 C++ 타입 이름이다(§11.2).
        const char* const* FieldTypeNames()
        {
            static const char* names[static_cast<int>(ScriptProject::FieldType::Count)] = {};
            if (names[0] == nullptr)
            {
                for (int index = 0; index < static_cast<int>(ScriptProject::FieldType::Count); ++index)
                {
                    names[index] = ScriptProject::FieldCppType(static_cast<ScriptProject::FieldType>(index));
                }
            }
            return names;
        }

        const char* NameProblemText(ScriptProject::NameProblem problem)
        {
            switch (problem)
            {
            case ScriptProject::NameProblem::NotIdentifier:
                return Loc::TextOr(LocKeys::NewScriptInvalidName,
                    "Use letters, digits and underscores, and do not start with a digit");
            case ScriptProject::NameProblem::Reserved:
                return Loc::TextOr(LocKeys::NewScriptReservedName, "C++ or the engine already uses that name");
            case ScriptProject::NameProblem::TakenByType:
                return Loc::TextOr(LocKeys::NewScriptTypeTaken, "A component or script with that name already exists");
            case ScriptProject::NameProblem::TakenByFile:
                return Loc::TextOr(LocKeys::NewScriptFileTaken, "A file with that name is already in this folder");
            default:
                return nullptr;
            }
        }
    }

    NewScriptPopup::NewScriptPopup(const char* folder)
        : m_folder(folder != nullptr ? folder : "")
    {
    }

    const char* NewScriptPopup::GetTitle() const
    {
        return Loc::TextOr(LocKeys::AssetsNewScript, "New Script");
    }

    const char* NewScriptPopup::GetId() const
    {
        return "new_script";
    }

    float NewScriptPopup::GetInitialWidth() const
    {
        return 480.0f;
    }

    void NewScriptPopup::OnDraw(EditorApplication& editor)
    {
        bool submitted = false;
        {
            Widget::FormLayout layout("##newScript");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewScriptName, "Name"))
                    .Tooltip(Loc::TextOr(LocKeys::NewScriptNameHint,
                        "The class name. Canvas files save the script under this name."))
                    .Required(),
                [&]() {
                    if (ImGui::IsWindowAppearing())
                    {
                        ImGui::SetKeyboardFocusHere();
                    }
                    submitted = Widget::TextField("##name", m_name).CommitOnEnter().MaxLength(64)();
                });
        }
        const ScriptProject::NameProblem nameProblem = editor.CheckScriptName(m_folder.c_str(), m_name.c_str());
        if (const char* text = NameProblemText(nameProblem))
        {
            Widget::ValidationMessage(Widget::Severity::Error, text)();
        }

        // **필드는 고급 옵션이다.** 접힌 채로 뜬다 - 필드는 헤더에 `JBRO_FIELD` 한 줄씩이라 나중에 적어도 된다.
        bool fieldsValid = true;
        for (std::size_t index = 0; index < m_fields.Size(); ++index)
        {
            fieldsValid = fieldsValid && ScriptProject::CheckFieldName(m_fields, index) == ScriptProject::NameProblem::None;
        }
        if (Widget::CollapsingSection(Loc::TextOr(LocKeys::NewScriptAdvanced, "Advanced Options"), false))
        {
            Widget::Text(Loc::TextOr(LocKeys::NewScriptFields, "Fields"));
            Widget::List("##fields", m_fields,
                [&](ScriptProject::FieldSpec& field, int) {
                    const float width = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(width * 0.6f);
                    Widget::TextField("##fieldName", field.name).Hint("Speed").MaxLength(64).Draw();
                    ImGui::SameLine();
                    int type = static_cast<int>(field.type);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (Widget::FilterCombo("##fieldType",
                            ArrayView<const char* const>(FieldTypeNames(), static_cast<std::size_t>(ScriptProject::FieldType::Count)),
                            type).ShowFilter(false).Draw())
                    {
                        field.type = static_cast<ScriptProject::FieldType>(type);
                    }
                },
                ScriptProject::FieldSpec{});
            if (false == fieldsValid)
            {
                Widget::ValidationMessage(Widget::Severity::Error, Loc::TextOr(LocKeys::NewScriptInvalidField,
                    "A field name is empty, cannot be used, or appears twice"))();
            }
        }
        else if (false == fieldsValid)
        {
            // 접은 채로도 왜 만들기가 막혔는지 보여야 한다.
            Widget::ValidationMessage(Widget::Severity::Error, Loc::TextOr(LocKeys::NewScriptInvalidField,
                "A field name is empty, cannot be used, or appears twice"))();
        }
        if (false == m_error.empty())
        {
            Widget::ValidationMessage(Widget::Severity::Error, m_error.c_str())();
        }
        ImGui::Spacing();

        const bool canCreate = nameProblem == ScriptProject::NameProblem::None && fieldsValid;
        {
            Widget::DisableScope disabled(false == canCreate);
            if (Widget::ActionButton(Loc::TextOr(LocKeys::CommonCreate, "Create"), Widget::Severity::Success))
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
            String failure;
            const String created = editor.CreateScript(m_folder.c_str(), m_name.c_str(), m_fields, failure);
            if (false == created.empty())
            {
                // 편집기로 저절로 열지는 않는다 - 어느 편집기로 열지는 아직 정하지 않았다(cpp-script-plan §4).
                // 에셋 브라우저가 그 폴더를 보이고, 두 번 누르면 연다.
                Close();
                return;
            }
            // 까닭은 번역된 한 문장이고, 엔진이 적은 영어는 로그에 남는다.
            Log::Write(LogLevel::Warning, "script", "the script could not be created: %s", failure.c_str());
            m_error = Loc::TextOr(LocKeys::NewScriptFailed, "The script could not be created");
        }
    }
}
