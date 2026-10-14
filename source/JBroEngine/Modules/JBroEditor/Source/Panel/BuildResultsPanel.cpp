#include "BuildResultsPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>

#include <imgui.h>

namespace JBro
{
    const char* BuildResultsPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* BuildResultsPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelBuildResults, "Build Results");
    }

    Bool BuildResultsPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void BuildResultsPanel::OnDraw()
    {
        using State = EditorApplication::ScriptBuildState;
        const State state = m_editor->GetScriptBuildState();
        const Array<ScriptBuild::Diagnostic>& diagnostics = m_editor->GetScriptDiagnostics();
        Int32 errors = 0;
        Int32 warnings = 0;
        for (const ScriptBuild::Diagnostic& diagnostic : diagnostics)
        {
            errors += diagnostic.isError ? 1 : 0;
            warnings += diagnostic.isError ? 0 : 1;
        }

        {
            Widget::DisableScope disabled(state == State::Running);
            if (Widget::Button(state == State::Running ? Loc::TextOr(LocKeys::BuildResultsBuilding, "Building...")
                                                         : Loc::TextOr(LocKeys::BuildResultsBuild, "Build")))
            {
                m_editor->BuildScripts();
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::Button(Loc::TextOr(LocKeys::BuildResultsClear, "Clear")))
        {
            m_editor->ClearScriptDiagnostics();
        }
        ImGui::SameLine(0.0f, 12.0f);
        Widget::TextF(Loc::TextOr(LocKeys::BuildResultsErrors, "%d errors"), errors);
        ImGui::SameLine(0.0f, 8.0f);
        Widget::TextF(Loc::TextOr(LocKeys::BuildResultsWarnings, "%d warnings"), warnings);
        ImGui::Separator();

        if (state == State::Succeeded && diagnostics.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::BuildResultsSucceeded, "Built with no errors or warnings"));
            return;
        }
        if (diagnostics.IsEmpty() && state == State::Running)
        {
            // 빌드는 끝나야 진단이 온다. 빈 칸만 두면 멈춘 것처럼 보인다.
            Widget::HintText(Loc::TextOr(LocKeys::BuildResultsRunning, "Building the scripts..."));
            return;
        }
        if (diagnostics.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::BuildResultsEmpty, "No build yet"));
            return;
        }
        // **오류가 먼저다.** MSBuild 는 경고를 먼저 내기도 해서, 받은 차례대로 두면 고칠 줄이 경고 아래로 밀린다(사용자 시나리오 A).
        for (Int32 pass = 0; pass < 2; ++pass)
        {
            for (std::size_t index = 0; index < diagnostics.Size(); ++index)
            {
                const ScriptBuild::Diagnostic& diagnostic = diagnostics[index];
                if (diagnostic.isError != (pass == 0))
                {
                    continue;
                }
                Widget::IdScope id(static_cast<Int32>(index));
                char text[1024] = {};
                const char* file = diagnostic.file.empty() ? "" : EditorPaths::LeafOfPath(diagnostic.file);
                std::snprintf(text, sizeof(text), "%s %s(%u)  %s %s", diagnostic.isError ? "x" : "!",
                    file, diagnostic.line.Get(), diagnostic.code.c_str(), diagnostic.message.c_str());
                if (Widget::SelectableRow(text, false))
                {
                    m_editor->OpenScriptDiagnostic(index);
                }
                Widget::HoveredTooltip(Loc::TextOr(LocKeys::BuildResultsOpenHint, "Click to open this line in the editor"));
            }
        }
    }
}
