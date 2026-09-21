#include "ProjectSettingsPanel.h"

#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/TextField.h>

#include <imgui.h>

namespace JBro
{
    namespace
    {
        // 해상도의 한계다. 0 은 그릴 화면이 없다는 뜻이고, 위쪽은 사람이 실수로
        // 자릿수를 하나 더 치는 것을 막는 값이다.
        constexpr int MinResolution = 16;
        constexpr int MaxResolution = 16384;
    }

    const char* ProjectSettingsPanel::GetTitle() const
    {
        return "ProjectSettings";
    }

    const char* ProjectSettingsPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelProjectSettings, "Project Settings");
    }

    bool ProjectSettingsPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        // 늘 보는 창이 아니다. 창 메뉴에서 열어 본다.
        SetOpen(false);
        return true;
    }

    void ProjectSettingsPanel::Reload()
    {
        m_draft = m_editor->GetProjectFile();
        m_loadedPath = m_editor->GetProjectFilePath();
        m_loaded = true;
        m_message.clear();
        m_messageIsError = false;
    }

    void ProjectSettingsPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        const String& path = m_editor->GetProjectFilePath();
        if (path.empty())
        {
            // 파일로 열지 않은 프로젝트다(테스트의 것이 그렇다). 고칠 파일이 없다.
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::ProjectSettingsNoFile, "this project has no file to edit"));
            m_loaded = false;
            return;
        }
        // 프로젝트가 바뀌었으면 다시 읽는다. 앞 프로젝트의 값을 다음 파일에 쓰면 안 된다.
        if (false == m_loaded || m_loadedPath != path)
        {
            Reload();
        }

        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsGeneral, "General")).Draw();
        {
            Widget::FormLayout layout("##general");
            // 엔진 판과 차원은 **보여만 준다.** 판은 런처가 어느 설치를 띄울지 고르는 값이고,
            // 차원은 열려 있는 프로젝트에서 바꾸면 지금 선 프레임워크와 어긋난다.
            layout.Row(
                [] { ImGui::TextUnformatted("EngineVersion"); },
                [&]
                {
                    Widget::DisableScope disabled(true);
                    ImGui::TextUnformatted(m_draft.engineVersion.c_str());
                });
            layout.Row(
                [] { ImGui::TextUnformatted("Framework"); },
                [&]
                {
                    Widget::DisableScope disabled(true);
                    ImGui::TextUnformatted(
                        m_draft.framework == FrameworkKind::Framework3D ? "3D" : "2D");
                });
            layout.Row(
                [] { ImGui::TextUnformatted("ResolutionWidth"); },
                [&]
                {
                    int value = static_cast<int>(m_draft.resolutionWidth);
                    if (Widget::DragInt("##width").Range(MinResolution, MaxResolution).Draw(value))
                    {
                        m_draft.resolutionWidth = static_cast<std::uint32_t>(value);
                    }
                });
            layout.Row(
                [] { ImGui::TextUnformatted("ResolutionHeight"); },
                [&]
                {
                    int value = static_cast<int>(m_draft.resolutionHeight);
                    if (Widget::DragInt("##height").Range(MinResolution, MaxResolution).Draw(value))
                    {
                        m_draft.resolutionHeight = static_cast<std::uint32_t>(value);
                    }
                });
            layout.Row(
                [] { ImGui::TextUnformatted("TextureFilter"); },
                [&]
                {
                    // 둘뿐이다(`Default` 는 텍스처의 임포트 옵션에만 있다, D-117).
                    bool linear = m_draft.textureFilter == TextureFilter::Linear;
                    if (Widget::Checkbox("##filter", linear))
                    {
                        m_draft.textureFilter =
                            linear ? TextureFilter::Linear : TextureFilter::Nearest;
                    }
                    Widget::HoveredTooltip(Loc::TextOr(
                        LocKeys::ProjectSettingsLinearFilter,
                        "linear sampling; off is nearest, which is the pixel-art default"));
                });
            layout.Row(
                [] { ImGui::TextUnformatted("DebugModeEnabled"); },
                [&] { Widget::Checkbox("##debug", m_draft.debugModeEnabled); });
        }

        // **에디터 언어**(D-146). 기존 엔진도 설정 창에서 골랐고, 고른 값은 프로젝트에 남는다.
        // 저장 단추를 기다리지 않고 **고르는 즉시 바뀐다** - 글자가 바뀌는 것을 눈으로 보고
        // 고르는 일이라, 저장한 뒤에야 바뀌면 무엇을 고른 것인지 알 수 없다.
        {
            const Array<String> locales = m_editor->GetAvailableLocales();
            Widget::FormLayout layout("##localization");
            layout.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::ProjectSettingsLanguage, "Language")),
                [&]() {
                    if (locales.IsEmpty())
                    {
                        ImGui::TextDisabled("%s", Loc::TextOr(LocKeys::ProjectSettingsNoLanguages,
                            "no language files were found"));
                        return;
                    }
                    if (false == ImGui::BeginCombo("##language", m_editor->GetEditorLocale().c_str()))
                    {
                        return;
                    }
                    for (std::size_t index = 0; index < locales.Size(); ++index)
                    {
                        const bool chosen = locales[index] == m_editor->GetEditorLocale();
                        if (ImGui::Selectable(locales[index].c_str(), chosen)
                            && false == chosen)
                        {
                            m_editor->SetEditorLocale(locales[index].c_str());
                            m_draft.editorLocale = m_editor->GetEditorLocale();
                        }
                        if (chosen)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                });
        }

        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsPaths, "Paths")).SpacingBefore().Draw();
        {
            Widget::FormLayout layout("##paths");
            layout.Row(
                [] { ImGui::TextUnformatted("AssetDirectory"); },
                [&] { Widget::TextField("##assets", m_draft.assetDirectory).Draw(); });
            layout.Row(
                [] { ImGui::TextUnformatted("ScriptSourceDirectory"); },
                [&] { Widget::TextField("##scriptSource", m_draft.scriptSourceDirectory).Draw(); });
            layout.Row(
                [] { ImGui::TextUnformatted("ScriptOutputLibraryPath"); },
                [&] { Widget::TextField("##scriptOut", m_draft.scriptOutputLibraryPath).Draw(); });
            layout.Row(
                [] { ImGui::TextUnformatted("LastOpenedCanvasPath"); },
                [&] { Widget::TextField("##lastCanvas", m_draft.lastOpenedCanvasPath).Draw(); });
        }

        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsBuild, "Build")).SpacingBefore().Draw();
        {
            Widget::FormLayout layout("##build");
            layout.Row(
                [] { ImGui::TextUnformatted("ProductName"); },
                [&] { Widget::TextField("##product", m_draft.build.productName).Draw(); });
            layout.Row(
                [] { ImGui::TextUnformatted("OutputDirectory"); },
                [&] { Widget::TextField("##output", m_draft.build.outputDirectory).Draw(); });
            layout.Row(
                [] { ImGui::TextUnformatted("StartupCanvas"); },
                [&] { Widget::TextField("##startup", m_draft.build.startupCanvas).Draw(); });
            layout.Row(
                [] { ImGui::TextUnformatted("EnableWindows"); },
                [&] { Widget::Checkbox("##windows", m_draft.build.enableWindows); });
            layout.Row(
                [] { ImGui::TextUnformatted("EnableWeb"); },
                [&] { Widget::Checkbox("##web", m_draft.build.enableWeb); });
            layout.Row(
                [] { ImGui::TextUnformatted("EnableAndroid"); },
                [&] { Widget::Checkbox("##android", m_draft.build.enableAndroid); });
            layout.Row(
                [] { ImGui::TextUnformatted("EnableIOS"); },
                [&] { Widget::Checkbox("##ios", m_draft.build.enableIOS); });
        }

        ImGui::Spacing();
        if (ImGui::Button(Loc::TextOr(LocKeys::ProjectSettingsSave, "Save")))
        {
            // **여기서야 파일에 간다.** 고치는 동안 파일을 건드리면 되돌릴 방법이 없다.
            ProjectFileError error;
            if (m_editor->SaveProjectSettings(m_draft, error))
            {
                m_message = Loc::TextOr(LocKeys::ProjectSettingsSaved, "saved");
                m_messageIsError = false;
                Log::Write(LogLevel::Info, "project", "settings saved: %s", path.c_str());
            }
            else
            {
                m_message = error.message;
                m_messageIsError = true;
                Log::Write(LogLevel::Error, "project",
                    "the settings could not be saved: %s", error.message.c_str());
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button(Loc::TextOr(LocKeys::ProjectSettingsRevert, "Revert")))
        {
            Reload();
        }
        if (false == m_message.empty())
        {
            Widget::ValidationMessage(
                m_messageIsError ? Widget::Severity::Error : Widget::Severity::Info,
                m_message.c_str()).Draw();
        }
    }
}
