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
        return "BuildResults";
    }

    const char* BuildResultsPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelBuildResults, "Build Results");
    }

    bool BuildResultsPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void BuildResultsPanel::OnDraw()
    {
        using State = EditorApplication::ScriptBuildState;
        const State state = m_editor->GetScriptBuildState();
        const Array<ScriptBuild::Diagnostic>& diagnostics = m_editor->GetScriptDiagnostics();
        int errors = 0;
        int warnings = 0;
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
            Widget::HintText(Loc::TextOr(LocKeys::BuildResultsSucceeded, "Built. The new code loads when the project is reopened"));
            return;
        }
        if (diagnostics.IsEmpty() && state != State::Running)
        {
            Widget::HintText(Loc::TextOr(LocKeys::BuildResultsEmpty, "No build yet"));
            return;
        }
        for (std::size_t index = 0; index < diagnostics.Size(); ++index)
        {
            const ScriptBuild::Diagnostic& diagnostic = diagnostics[index];
            Widget::IdScope id(static_cast<int>(index));
            char text[1024] = {};
            const char* file = diagnostic.file.empty() ? "" : EditorPaths::LeafOfPath(diagnostic.file);
            std::snprintf(text, sizeof(text), "%s %s(%u)  %s %s", diagnostic.isError ? "x" : "!",
                file, diagnostic.line, diagnostic.code.c_str(), diagnostic.message.c_str());
            if (ImGui::Selectable(text, false))
            {
                m_editor->OpenScriptDiagnostic(index);
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::BuildResultsOpenHint, "Click to open this line in the editor"));
        }
    }
}
