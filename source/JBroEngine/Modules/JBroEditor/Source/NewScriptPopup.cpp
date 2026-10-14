#include "NewScriptPopup.h"

#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Chips.h>
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

        // 마지막에 고른 언어다(0 은 C++, 1 은 JBroScript). 에디터를 켜 둔 동안 다음 팝업이 이어받는다 - JBroScript 로 쓰는 사람이
        // 스크립트마다 다시 고르지 않게.
        Int32& LastLanguage()
        {
            static Int32 language = 0;
            return language;
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
                    if (ImGui::IsWindowAppearing() || m_refocus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_refocus = false;
                    }
                    submitted = Widget::TextField("##name", m_name).CommitOnEnter().LiveText().MaxLength(64)();
                });
            // 언어 이름은 번역하지 않는다. JBroScript 는 2D 만 빌드되므로 3D 프로젝트에서는 줄 자체를 두지 않는다.
            if (editor.GetFrameworkKind() == FrameworkKind::Framework2D)
            {
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::NewScriptLanguage, "Language"))
                        .Tooltip(Loc::TextOr(LocKeys::NewScriptLanguageHint,
                            "JBroScript is turned into C++ when you build and goes into the same game DLL. 2D projects only")),
                    [&]() {
                        static const char* const languages[] = {"C++", "JBroScript"};
                        // 고른 뒤에는 이름 칸으로 돌아간다 - 칩을 누르면 포커스가 빠져, 이름을 치려면 칸을 다시 눌러야 했다.
                        if (Widget::ChoiceChips("##language", ArrayView<const char* const>(languages, 2), LastLanguage()))
                        {
                            m_refocus = true;
                        }
                    });
            }
        }
        const Bool jbroScript = editor.GetFrameworkKind() == FrameworkKind::Framework2D && LastLanguage() == 1;
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
        // 필드 고르기는 C++ 타입 이름을 쓴다. JBroScript 는 필드를 파일에 바로 적는다(`[prop] public Float speed = 1.0`).
        if (jbroScript)
        {
            fieldsValid = true;
        }
        else if (Widget::CollapsingSection(Loc::TextOr(LocKeys::NewScriptAdvanced, "Advanced Options"), false))
        {
            Widget::Text(Loc::TextOr(LocKeys::NewScriptFields, "Fields"));
            const OfferedFieldTypes& offered = FieldTypesFor(editor.GetFrameworkKind());
            Widget::List("##fields", m_fields,
                [&](ScriptProject::FieldSpec& field, Int32) {
                    // **목록이 준 내용 폭 안에서 나눈다.** 창의 남은 폭을 다 쓰면 줄 끝의 지우기 단추(X)를 종류 칸이 덮는다.
                    const float width = ImGui::CalcItemWidth();
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
                    ImGui::SetNextItemWidth(width * 0.4f - ImGui::GetStyle().ItemSpacing.x);
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
        if (submitted && false == canCreate)
        {
            m_refocus = true;
        }
        if (submitted && canCreate)
        {
            String failure;
            const String created = jbroScript ? editor.CreateJBroScript(m_folder.c_str(), m_name.c_str(), failure)
                                              : editor.CreateScript(m_folder.c_str(), m_name.c_str(), m_fields, failure);
            if (false == created.empty())
            {
                // 편집기를 저절로 띄우지는 않는다 - Visual Studio 는 뜨는 데 오래 걸리고, 사용자가 이어서 스크립트를 더 만들 수 있다.
                // 대신 만들었다는 것과 다음에 할 일을 알리고, 그 알림을 누르면 편집기 설정에서 고른 편집기로 연다.
                editor.NotifyScriptCreated(created, m_name.c_str());
                Close();
                return;
            }
            // 까닭은 번역된 한 문장이고, 엔진이 적은 영어는 로그에 남는다.
            Log::Write(LogLevel::Warning, "script", "the script could not be created: %s", failure.c_str());
            m_error = Loc::TextOr(LocKeys::NewScriptFailed, "The script could not be created");
            m_refocus = true;
        }
    }
}
