#include "pch.h"
#include "ProjectSettingsWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

namespace
{
    SafePtr<CProjectManager> GetProjectManagerForSettings()
    {
        return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
    }

    const char* ToScriptStateText(ELiveCompileState state)
    {
        switch (state)
        {
        case ELiveCompileState::Idle:
            return Loc::Text("script.status.idle");
        case ELiveCompileState::Compiling:
            return Loc::Text("script.status.building");
        case ELiveCompileState::Loaded:
            return Loc::Text("script.status.loaded");
        case ELiveCompileState::Failed:
            return Loc::Text("script.status.failed");
        default:
            return Loc::Text("script.status.unknown");
        }
    }

    ImVec4 ToScriptStateColor(ELiveCompileState state)
    {
        switch (state)
        {
        case ELiveCompileState::Loaded:
            return ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
        case ELiveCompileState::Failed:
            return ImVec4(0.95f, 0.35f, 0.3f, 1.0f);
        case ELiveCompileState::Compiling:
            return ImVec4(0.9f, 0.75f, 0.35f, 1.0f);
        default:
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        }
    }
}

void CProjectSettingsWindow::OnCreate()
{
    SetLocalizedTitleKey("window.project_settings");

    m_imguiFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize;

    m_windowFlags = IMWINDOW_FLAG_NONE;

    SetSize({ 560.0f, 520.0f });
    SetVisible(false);
}

void CProjectSettingsWindow::OnShow()
{
    // 창이 열릴 때마다 현재 프로젝트 설정값으로 초기화합니다.
    SafePtr<CProjectManager> pm = GetProjectManagerForSettings();
    if (pm)
    {
        m_editResW = static_cast<int>(pm->GetResolutionWidth());
        m_editResH = static_cast<int>(pm->GetResolutionHeight());
        m_editPPU  = pm->GetPixelsPerUnit();

        m_scriptBuildConfiguration = EScriptBuildConfiguration::Release == pm->GetScriptBuildConfiguration() ? 1 : 0;
        m_scriptAutoRebuildEnabled = pm->IsScriptAutoRebuildEnabled();
    }

    if (Core::Localization.IsValid())
    {
        const std::vector<LocalizationLocaleInfo>& locales = Core::Localization->GetSupportedLocales();
        const std::string& currentLocale = Core::Localization->GetCurrentLocale();
        m_selectedLocaleIndex = 0;
        for (std::size_t i = 0; i < locales.size(); ++i)
        {
            if (locales[i].Code == currentLocale)
            {
                m_selectedLocaleIndex = static_cast<int>(i);
                break;
            }
        }
    }
}

void CProjectSettingsWindow::OnRenderStay()
{
    SafePtr<CProjectManager> pm = GetProjectManagerForSettings();

    // ── 해상도 섹션 ──────────────────────────────────────────────────
    ImGui::SeparatorText(Loc::Text("project_settings.resolution"));

    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputInt(Loc::Text("project_settings.width_px"),  &m_editResW);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputInt(Loc::Text("project_settings.height_px"), &m_editResH);

    // 0 이하 값 방지
    if (m_editResW < 1) m_editResW = 1;
    if (m_editResH < 1) m_editResH = 1;

    // ── 좌표계 섹션 ───────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText(Loc::Text("project_settings.coordinates"));

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat(Loc::Text("project_settings.pixels_per_unit"), &m_editPPU, 1.0f, 1.0f, 10000.0f, "%.1f");
    if (m_editPPU < 1.0f) m_editPPU = 1.0f;
    ImGui::TextDisabled(
        Loc::Text("project_settings.ppu_help"),
        m_editPPU, 1.0f / m_editPPU);

    // ── Script 섹션 ─────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText(Loc::Text("project_settings.script"));

    if (pm)
    {
        const std::string scriptPath = pm->GetScriptPath().generic_string();
        ImGui::TextDisabled("%s: %s", Loc::Text("project_settings.user_scripts"), scriptPath.c_str());
    }
    ImGui::SetNextItemWidth(180.0f);
    const char* scriptConfigs[] = { "Debug", "Release" };
    ImGui::Combo(Loc::Text("project_settings.script_build"), &m_scriptBuildConfiguration, scriptConfigs, IM_ARRAYSIZE(scriptConfigs));
    ImGui::TextDisabled("%s", Loc::Text("project_settings.script_build_help"));

    const ELiveCompileState liveState = pm ? pm->GetLiveCompileState() : ELiveCompileState::Idle;
    ImGui::TextColored(ToScriptStateColor(liveState), ToScriptStateText(liveState));

    if (ImGui::Checkbox(Loc::Text("project_settings.script_auto_rebuild"), &m_scriptAutoRebuildEnabled))
    {
        if (pm)
        {
            // 토글 시 메모리뿐 아니라 .Jproject 에도 즉시 반영 — Apply 없이 닫아도
            // 다음 세션에서 같은 상태가 유지되도록.
            pm->SetScriptAutoRebuildEnabled(m_scriptAutoRebuildEnabled);
            pm->SaveProject();
        }
    }
    ImGui::TextDisabled("%s", Loc::Text("project_settings.script_auto_rebuild_help"));

    if (ImGui::Button(Loc::Text("project_settings.script_rebuild")))
    {
        if (pm)
        {
            pm->SetScriptBuildConfiguration(1 == m_scriptBuildConfiguration
                ? EScriptBuildConfiguration::Release
                : EScriptBuildConfiguration::Debug);
            pm->RebuildScriptModule();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Loc::Text("common.unload")))
    {
        if (pm)
        {
            pm->StopLiveCompile();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(Loc::Text("project_settings.localization"));

    if (Core::Localization.IsValid())
    {
        const std::vector<LocalizationLocaleInfo>& locales = Core::Localization->GetSupportedLocales();
        if (false == locales.empty())
        {
            if (m_selectedLocaleIndex < 0 || m_selectedLocaleIndex >= static_cast<int>(locales.size()))
            {
                m_selectedLocaleIndex = 0;
            }

            ImGui::SetNextItemWidth(220.0f);
            const LocalizationLocaleInfo& selectedLocale = locales[static_cast<std::size_t>(m_selectedLocaleIndex)];
            if (ImGui::BeginCombo(Loc::Text("project_settings.language"), selectedLocale.DisplayName.c_str()))
            {
                for (std::size_t i = 0; i < locales.size(); ++i)
                {
                    const bool selected = static_cast<int>(i) == m_selectedLocaleIndex;
                    if (ImGui::Selectable(locales[i].DisplayName.c_str(), selected))
                    {
                        m_selectedLocaleIndex = static_cast<int>(i);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 버튼 ──────────────────────────────────────────────────────────
    if (ImGui::Button(Loc::Text("common.apply"), { 100.0f, 0.0f }))
    {
        if (pm)
        {
            // 값만 메모리에 반영. 실제 파일 저장은 "프로젝트 저장" 메뉴에서 수행.
            pm->SetResolution(
                static_cast<std::uint32_t>(m_editResW),
                static_cast<std::uint32_t>(m_editResH));
            pm->SetPixelsPerUnit(m_editPPU);
            pm->SetScriptBuildConfiguration(1 == m_scriptBuildConfiguration
                ? EScriptBuildConfiguration::Release
                : EScriptBuildConfiguration::Debug);
            pm->SetScriptAutoRebuildEnabled(m_scriptAutoRebuildEnabled);
            pm->SaveProject();
        }
        if (Core::Localization.IsValid())
        {
            const std::vector<LocalizationLocaleInfo>& locales = Core::Localization->GetSupportedLocales();
            if (m_selectedLocaleIndex >= 0 && m_selectedLocaleIndex < static_cast<int>(locales.size()))
            {
                const std::string& localeCode = locales[static_cast<std::size_t>(m_selectedLocaleIndex)].Code;
                if (Core::Localization->SetCurrentLocale(localeCode) && pm)
                {
                    pm->SetEditorLocaleCode(localeCode);
                }
            }
        }
        SetVisible(false);
    }

    ImGui::SameLine();

    if (ImGui::Button(Loc::Text("common.cancel"), { 100.0f, 0.0f }))
    {
        SetVisible(false);
    }
}
