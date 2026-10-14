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
        // 이 프로젝트의 차원에서 고를 수 있는 것만 차례대로 든다(D-302 (17)). 2D 프로젝트에 3D 핸들을 내놓으면 만든 헤더가 컴파일되지 않는다.
        struct OfferedFieldTypes
        {
            const char* names[static_cast<Int32>(ScriptProject::FieldType::Count)] = {};
            ScriptProject::FieldType types[static_cast<Int32>(ScriptProject::FieldType::Count)] = {};
            std::size_t count = 0;
        };

        const OfferedFieldTypes& FieldTypesFor(FrameworkKind framework)
        {
            static OfferedFieldTypes offered[2];
            OfferedFieldTypes& list = offered[framework == FrameworkKind::Framework3D ? 1 : 0];
            if (list.count == 0)
            {
                for (Int32 index = 0; index < static_cast<Int32>(ScriptProject::FieldType::Count); ++index)
                {
                    const auto type = static_cast<ScriptProject::FieldType>(index.Get());
                    if (ScriptProject::IsFieldTypeOffered(type, framework))
                    {
                        list.names[list.count] = ScriptProject::FieldCppType(type);
                        list.types[list.count] = type;
                        ++list.count;
                    }
                }
            }
            return list;
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

    Float NewScriptPopup::GetInitialWidth() const
    {
        return 480.0f;
    }

    void NewScriptPopup::OnDraw(EditorApplication& editor)
    {
        Bool submitted = false;
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
        Bool fieldsValid = true;
        for (std::size_t index = 0; index < m_fields.Size(); ++index)
        {
            fieldsValid = fieldsValid && ScriptProject::CheckFieldName(m_fields, index) == ScriptProject::NameProblem::None;
        }
        if (Widget::CollapsingSection(Loc::TextOr(LocKeys::NewScriptAdvanced, "Advanced Options"), false))
        {
            Widget::Text(Loc::TextOr(LocKeys::NewScriptFields, "Fields"));
            const OfferedFieldTypes& offered = FieldTypesFor(editor.GetFrameworkKind());
            Widget::List("##fields", m_fields,
                [&](ScriptProject::FieldSpec& field, Int32) {
                    const float width = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(width * 0.6f);
                    Widget::TextField("##fieldName", field.name).Hint("Speed").MaxLength(64).Draw();
                    ImGui::SameLine();
                    Int32 type = -1;
                    for (std::size_t at = 0; at < offered.count; ++at)
                    {
                        if (offered.types[at] == field.type)
                        {
                            type = static_cast<JBro::Int32>(at);
                        }
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    // 종류가 많아졌으므로(핸들 20 종) 거르기 칸을 둔다.
                    if (Widget::FilterCombo("##fieldType", ArrayView<const char* const>(offered.names, offered.count), type).Draw()
                        && type >= 0 && static_cast<std::size_t>(type.Get()) < offered.count)
                    {
                        field.type = offered.types[static_cast<std::size_t>(type.Get())];
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

        const Bool canCreate = nameProblem == ScriptProject::NameProblem::None && fieldsValid;
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
