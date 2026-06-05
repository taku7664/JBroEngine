#include "pch.h"
#include "ProjectSettingsWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"
#include "Engine/Editor/ImItem/ImSplitter.h"   // ImGui::Utillity::VerticalSplitter
#include "Engine/Editor/ImItem/ImText.h"       // ImText (라벨 + 설명 툴팁)
#include "Engine/Editor/ImGuiUtillity.h"       // ImGui::Utillity::FormLayout, HoveredToolTip

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"

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
        ImGuiWindowFlags_NoCollapse;

    m_windowFlags = IMWINDOW_FLAG_NONE;

    SetSize({ 760.0f, 560.0f });
    SetVisible(false);
}

void CProjectSettingsWindow::OnShow()
{
    m_errorMessage.clear();

    // 창이 열릴 때마다 현재 프로젝트 설정값으로 초기화합니다.
    SafePtr<CProjectManager> pm = GetProjectManagerForSettings();
    if (pm)
    {
        m_editResW = static_cast<int>(pm->GetResolutionWidth());
        m_editResH = static_cast<int>(pm->GetResolutionHeight());
        m_editPPU  = pm->GetPixelsPerUnit();

        m_scriptBuildConfiguration = EScriptBuildConfiguration::Release == pm->GetScriptBuildConfiguration() ? 1 : 0;
        m_scriptAutoRebuildEnabled = pm->IsScriptAutoRebuildEnabled();
        m_debugModeEnabled = pm->IsDebugModeEnabled();
        m_editAssetWatchIgnorePatterns = pm->GetAssetWatchIgnorePatterns();

        // InputTextMultiline 백킹 버퍼를 패턴 벡터로부터 재구축 (프로젝트 전환 시 stale 방지).
        m_assetWatchIgnoreBuffer.clear();
        for (const std::string& line : m_editAssetWatchIgnorePatterns)
        {
            m_assetWatchIgnoreBuffer += line;
            m_assetWatchIgnoreBuffer.push_back('\n');
        }
    }

    if (Engine.Localization.IsValid())
    {
        const std::vector<LocalizationLocaleInfo>& locales = Engine.Localization->GetSupportedLocales();
        const std::string& currentLocale = Engine.Localization->GetCurrentLocale();
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
    // ── 레이아웃: 좌측 카테고리 | Splitter | 우측 콘텐츠 (Inspector 패턴) ──
    constexpr float SPLITTER_W = 3.0f;
    constexpr float MIN_RATIO  = 0.18f;
    constexpr float MAX_RATIO  = 0.55f;
    constexpr float FOOTER_H   = 44.0f;   // Apply/Cancel 영역 높이 예약

    const ImVec2 totalAvail = ImGui::GetContentRegionAvail();
    const ImVec2 bodyAvail(totalAvail.x, totalAvail.y - FOOTER_H);

    const float leftW  = bodyAvail.x * m_splitRatio - SPLITTER_W * 0.5f;
    const float rightW = bodyAvail.x - leftW - SPLITTER_W;

    // ── 좌측 카테고리 패널 ─────────────────────────────────────────────
    ImGui::BeginChild("##project_settings_categories",
        ImVec2(leftW, bodyAvail.y), true, ImGuiWindowFlags_NoScrollbar);
    DrawCategoryList(leftW);
    ImGui::EndChild();

    // ── Splitter ───────────────────────────────────────────────────────
    ImGui::SameLine();
    ImGui::Utillity::VerticalSplitter("##ProjectSettingsSplitter",
        m_splitRatio, bodyAvail, MIN_RATIO, MAX_RATIO, SPLITTER_W);
    ImGui::SameLine();

    // ── 우측 콘텐츠 패널 ───────────────────────────────────────────────
    ImGui::BeginChild("##project_settings_content",
        ImVec2(rightW, bodyAvail.y), true);
    DrawCategoryContent(rightW);
    ImGui::EndChild();

    // ── 하단 Apply/Cancel ─────────────────────────────────────────────
    ImGui::Separator();
    DrawFooterButtons();
}

void CProjectSettingsWindow::DrawCategoryList(float)
{
    struct CategoryEntry { ECategory Kind; const char* LocKey; };
    static const CategoryEntry kCategories[] = {
        { ECategory::General,      "project_settings.category.general"       },
        { ECategory::Script,       "project_settings.category.script"        },
        { ECategory::Localization, "project_settings.category.localization"  },
        { ECategory::Audio,        "project_settings.category.audio"         },
        { ECategory::Debug,        "project_settings.category.debug"         },
        { ECategory::AssetWatcher, "project_settings.category.asset_watcher" },
    };

    for (const CategoryEntry& entry : kCategories)
    {
        const bool selected = (entry.Kind == m_selectedCategory);
        if (ImGui::Selectable(Loc::Text(entry.LocKey), selected))
        {
            m_selectedCategory = entry.Kind;
        }
    }
}

void CProjectSettingsWindow::DrawCategoryContent(float)
{
    switch (m_selectedCategory)
    {
    case ECategory::General:      DrawCategoryGeneral();      break;
    case ECategory::Script:       DrawCategoryScript();       break;
    case ECategory::Localization: DrawCategoryLocalization(); break;
    case ECategory::Audio:        DrawCategoryAudio();        break;
    case ECategory::Debug:        DrawCategoryDebug();        break;
    case ECategory::AssetWatcher: DrawCategoryAssetWatcher(); break;
    default: break;
    }
}

void CProjectSettingsWindow::DrawCategoryGeneral()
{
    ImGui::SeparatorText(Loc::Text("project_settings.resolution"));
    {
        ImGui::Utillity::FormLayout layout("##ps_general_resolution", 4.0f, {2.0f, 1.0f}, 120.0f);
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.width_px.desc"));
                label(Loc::Text("project_settings.width_px"));
            },
            [&]() { ImGui::InputInt("##ps.width_px", &m_editResW); });
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.height_px.desc"));
                label(Loc::Text("project_settings.height_px"));
            },
            [&]() { ImGui::InputInt("##ps.height_px", &m_editResH); });
    }
    if (m_editResW < 1) m_editResW = 1;
    if (m_editResH < 1) m_editResH = 1;

    ImGui::Spacing();
    ImGui::SeparatorText(Loc::Text("project_settings.coordinates"));
    {
        ImGui::Utillity::FormLayout layout("##ps_general_coords", 4.0f, {2.0f, 1.0f}, 120.0f);
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.pixels_per_unit.desc"));
                label(Loc::Text("project_settings.pixels_per_unit"));
            },
            [&]() { ImGui::DragFloat("##ps.ppu", &m_editPPU, 1.0f, 1.0f, 10000.0f, "%.1f"); });
    }
    if (m_editPPU < 1.0f) m_editPPU = 1.0f;
    ImGui::TextDisabled(Loc::Text("project_settings.ppu_help"),
        m_editPPU, 1.0f / m_editPPU);
}

void CProjectSettingsWindow::DrawCategoryScript()
{
    SafePtr<CProjectManager> pm = GetProjectManagerForSettings();

    ImGui::SeparatorText(Loc::Text("project_settings.script"));

    {
        ImGui::Utillity::FormLayout layout("##ps_script_form", 4.0f, {2.0f, 1.0f}, 140.0f);

        // 사용자 스크립트 경로 (읽기 전용 디스플레이)
        if (pm)
        {
            const std::string scriptPath = pm->GetScriptPath().generic_string();
            layout.Row(
                [&]() {
                    ImText label;
                    label.SetHoveredTooltip(Loc::Text("project_settings.user_scripts.desc"));
                    label(Loc::Text("project_settings.user_scripts"));
                },
                [&]() { ImGui::TextDisabled("%s", scriptPath.c_str()); });
        }

        // 빌드 구성
        const char* scriptConfigs[] = { "Debug", "Release" };
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.script_build.desc"));
                label(Loc::Text("project_settings.script_build"));
            },
            [&]() { ImGui::Combo("##ps.script_build", &m_scriptBuildConfiguration,
                scriptConfigs, IM_ARRAYSIZE(scriptConfigs)); });

        // 라이브 컴파일 상태
        const ELiveCompileState liveState = pm ? pm->GetLiveCompileState() : ELiveCompileState::Idle;
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.script_state.desc"));
                label(Loc::Text("project_settings.script_state"));
            },
            [&]() { ImGui::TextColored(ToScriptStateColor(liveState), "%s", ToScriptStateText(liveState)); });

        // 자동 리빌드
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.script_auto_rebuild.desc"));
                label(Loc::Text("project_settings.script_auto_rebuild"));
            },
            [&]() {
                if (ImGui::Checkbox("##ps.script_auto_rebuild", &m_scriptAutoRebuildEnabled))
                {
                    if (pm)
                    {
                        pm->SetScriptAutoRebuildEnabled(m_scriptAutoRebuildEnabled);
                        std::string error;
                        if (false == pm->SaveProject(&error))
                        {
                            m_errorMessage = false == error.empty() ? error : Loc::Text("project_settings.save_failed");
                        }
                        else
                        {
                            m_errorMessage.clear();
                        }
                    }
                }
            });
    }

    ImGui::Spacing();
    if (ImGui::Button(Loc::Text("project_settings.script_rebuild")))
    {
        if (pm)
        {
            pm->SetScriptBuildConfiguration(1 == m_scriptBuildConfiguration
                ? EScriptBuildConfiguration::Release : EScriptBuildConfiguration::Debug);
            pm->RebuildScriptModule();
        }
    }
    ImGui::Utillity::HoveredToolTip(Loc::Text("project_settings.script_rebuild.desc"));

    ImGui::SameLine();
    if (ImGui::Button(Loc::Text("common.unload")))
    {
        if (pm) pm->StopLiveCompile();
    }
    ImGui::Utillity::HoveredToolTip(Loc::Text("project_settings.script_unload.desc"));
}

void CProjectSettingsWindow::DrawCategoryLocalization()
{
    ImGui::SeparatorText(Loc::Text("project_settings.localization"));

    if (false == Engine.Localization.IsValid())
    {
        ImGui::TextDisabled("%s", Loc::Text("project_settings.localization_unavailable"));
        return;
    }

    const std::vector<LocalizationLocaleInfo>& locales = Engine.Localization->GetSupportedLocales();
    if (locales.empty())
    {
        ImGui::TextDisabled("%s", Loc::Text("project_settings.localization_no_locales"));
        return;
    }

    if (m_selectedLocaleIndex < 0 || m_selectedLocaleIndex >= static_cast<int>(locales.size()))
    {
        m_selectedLocaleIndex = 0;
    }

    const LocalizationLocaleInfo& selectedLocale = locales[static_cast<std::size_t>(m_selectedLocaleIndex)];
    ImGui::Utillity::FormLayout layout("##ps_localization_form", 4.0f, {2.0f, 1.0f}, 120.0f);
    layout.Row(
        [&]() {
            ImText label;
            label.SetHoveredTooltip(Loc::Text("project_settings.language.desc"));
            label(Loc::Text("project_settings.language"));
        },
        [&]() {
            if (ImGui::BeginCombo("##ps.language", selectedLocale.DisplayName.c_str()))
            {
                for (std::size_t i = 0; i < locales.size(); ++i)
                {
                    const bool selected = static_cast<int>(i) == m_selectedLocaleIndex;
                    if (ImGui::Selectable(locales[i].DisplayName.c_str(), selected))
                    {
                        m_selectedLocaleIndex = static_cast<int>(i);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });
}

void CProjectSettingsWindow::DrawCategoryAudio()
{
    ImGui::SeparatorText(Loc::Text("project_settings.audio"));

    {
        ImGui::Utillity::FormLayout layout("##ps_audio_form", 4.0f, {2.0f, 1.0f}, 140.0f);
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text("project_settings.audio.master_volume.desc"));
                label(Loc::Text("project_settings.audio.master_volume"));
            },
            [&]() { ImGui::DragFloat("##ps.master_volume", &m_masterVolume, 0.01f, 0.0f, 2.0f); });
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Loc::Text("project_settings.audio.buses_help"));
    // PR D 의 CAudioService 가 도입되면 여기에 버스별 볼륨 / 사용자 정의 버스
    // 추가 UI 가 들어간다.
}

void CProjectSettingsWindow::DrawCategoryDebug()
{
    ImGui::SeparatorText(Loc::Text("project_settings.debug"));

    ImGui::Utillity::FormLayout layout("##ps_debug_form", 4.0f, {2.0f, 1.0f}, 140.0f);
    layout.Row(
        [&]() {
            ImText label;
            label.SetHoveredTooltip(Loc::Text("project_settings.debug_mode.desc"));
            label(Loc::Text("project_settings.debug_mode"));
        },
        [&]() { ImGui::Checkbox("##ps.debug_mode", &m_debugModeEnabled); });
}

void CProjectSettingsWindow::DrawCategoryAssetWatcher()
{
    ImGui::SeparatorText(Loc::Text("project_settings.asset_watcher.title"));
    ImGui::TextWrapped("%s", Loc::Text("project_settings.asset_watcher.desc"));
    ImGui::Spacing();

    // 백킹 버퍼는 멤버(m_assetWatchIgnoreBuffer) — OnShow 에서 패턴 벡터로부터 재구축됨.
    // 편집 발생 시 다음 프레임에 벡터로 재파싱.
    const ImVec2 boxSize(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() * 14.0f);
    m_assetWatchIgnoreBuffer.reserve(m_assetWatchIgnoreBuffer.size() + 1024);
    if (ImGui::InputTextMultiline("##ps.asset_watcher.patterns",
        m_assetWatchIgnoreBuffer.data(), m_assetWatchIgnoreBuffer.capacity(),
        boxSize, ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) -> int
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                std::string* buf = static_cast<std::string*>(data->UserData);
                buf->resize(data->BufTextLen);
                data->Buf = buf->data();
            }
            return 0;
        }, &m_assetWatchIgnoreBuffer))
    {
        m_editAssetWatchIgnorePatterns.clear();
        std::size_t start = 0;
        for (std::size_t i = 0; i <= m_assetWatchIgnoreBuffer.size(); ++i)
        {
            if (i == m_assetWatchIgnoreBuffer.size() || '\n' == m_assetWatchIgnoreBuffer[i] || '\r' == m_assetWatchIgnoreBuffer[i])
            {
                if (i > start)
                {
                    std::string line(m_assetWatchIgnoreBuffer, start, i - start);
                    while (false == line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
                    while (false == line.empty() && (line.back()  == ' ' || line.back()  == '\t')) line.pop_back();
                    if (false == line.empty()) m_editAssetWatchIgnorePatterns.push_back(std::move(line));
                }
                start = i + 1;
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Loc::Text("project_settings.asset_watcher.syntax_hint"));
}

void CProjectSettingsWindow::DrawFooterButtons()
{
    SafePtr<CProjectManager> pm = GetProjectManagerForSettings();

    if (false == m_errorMessage.empty())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "%s", m_errorMessage.c_str());
    }

    const bool applied = ImGui::Button(Loc::Text("common.apply"), { 100.0f, 0.0f });
    ImGui::Utillity::HoveredToolTip(Loc::Text("project_settings.apply.desc"));
    if (applied)
    {
        if (pm)
        {
            pm->SetResolution(static_cast<std::uint32_t>(m_editResW),
                              static_cast<std::uint32_t>(m_editResH));
            pm->SetPixelsPerUnit(m_editPPU);
            pm->SetScriptBuildConfiguration(1 == m_scriptBuildConfiguration
                ? EScriptBuildConfiguration::Release : EScriptBuildConfiguration::Debug);
            pm->SetScriptAutoRebuildEnabled(m_scriptAutoRebuildEnabled);
            pm->SetDebugModeEnabled(m_debugModeEnabled);
            pm->SetAssetWatchIgnorePatterns(m_editAssetWatchIgnorePatterns);
        }
        if (Engine.Localization.IsValid())
        {
            const std::vector<LocalizationLocaleInfo>& locales = Engine.Localization->GetSupportedLocales();
            if (m_selectedLocaleIndex >= 0 && m_selectedLocaleIndex < static_cast<int>(locales.size()))
            {
                const std::string& localeCode = locales[static_cast<std::size_t>(m_selectedLocaleIndex)].Code;
                if (Engine.Localization->SetCurrentLocale(localeCode) && pm)
                {
                    pm->SetEditorLocaleCode(localeCode);
                }
            }
        }
        std::string error;
        if (false == pm.IsValid() || false == pm->SaveProject(&error))
        {
            m_errorMessage = false == error.empty() ? error : Loc::Text("project_settings.save_failed");
        }
        else
        {
            m_errorMessage.clear();
            SetVisible(false);
        }
    }
    ImGui::SameLine();
    const bool cancelled = ImGui::Button(Loc::Text("common.cancel"), { 100.0f, 0.0f });
    ImGui::Utillity::HoveredToolTip(Loc::Text("project_settings.cancel.desc"));
    if (cancelled)
    {
        SetVisible(false);
    }
}
