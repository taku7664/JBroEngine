#include "pch.h"
#include "ProjectSettingsWindow.h"

#include "Engine/Editor/ImWindow/ImWindowFlag.h"
#include "Editor/ImItem/ImSplitter.h"   // ImGui::Utillity::VerticalSplitter
#include "Editor/ImItem/ImText.h"       // ImText (라벨 + 설명 툴팁)
#include "Editor/ImItem/ImNameListEdit.h"  // 한 줄 = 한 이름 목록 편집(입력 레이어)
#include "Editor/ImItem/ImList.h"         // 믹싱 버스 목록(이름 + 기본 음량)
#include "Engine/Editor/ImGuiUtillity.h"       // ImGui::Utillity::FormLayout, HoveredToolTip

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"

#include "Core/Input/InputAction.h"
#include "Core/Input/InputTypes.h"

#include <algorithm>   // std::clamp — 버스 행의 슬라이더 폭 제한

namespace
{
    const char* ToScriptStateText(ELiveCompileState state)
    {
        switch (state)
        {
        case ELiveCompileState::Idle:
            return Loc::Text(EditorLocKeys::ScriptStatusIdle);
        case ELiveCompileState::Compiling:
            return Loc::Text(EditorLocKeys::ScriptStatusBuilding);
        case ELiveCompileState::Loaded:
            return Loc::Text(EditorLocKeys::ScriptStatusLoaded);
        case ELiveCompileState::Failed:
            return Loc::Text(EditorLocKeys::ScriptStatusFailed);
        default:
            return Loc::Text(EditorLocKeys::ScriptStatusUnknown);
        }
    }

    EImValidationSeverity ToScriptStateSeverity(ELiveCompileState state)
    {
        switch (state)
        {
        case ELiveCompileState::Loaded:
            return EImValidationSeverity::Success;
        case ELiveCompileState::Failed:
            return EImValidationSeverity::Error;
        case ELiveCompileState::Compiling:
            return EImValidationSeverity::Warning;
        default:
            return EImValidationSeverity::Info;
        }
    }

    // 특정 enum 타입의 이름 콤보로 int code 를 편집. 변경되면 true.
    template<typename E>
    bool CodeComboFor(const char* id, int& code)
    {
        E current = static_cast<E>(code);
        if (ImEnumCombo<E>(id, current).Draw())
        {
            code = static_cast<int>(current);
            return true;
        }
        return false;
    }

    // 바인딩 Code 콤보 — Source 에 따라 보여줄 enum 이 달라진다.
    bool BindingCodeCombo(const char* id, EInputBindingSource source, int& code)
    {
        switch (source)
        {
        case EInputBindingSource::Key:           return CodeComboFor<EKeyCode>(id, code);
        case EInputBindingSource::MouseButton:   return CodeComboFor<EMouseButton>(id, code);
        case EInputBindingSource::GamepadButton: return CodeComboFor<EGamepadButton>(id, code);
        case EInputBindingSource::GamepadAxis:   return CodeComboFor<EGamepadAxis>(id, code);
        case EInputBindingSource::GamepadStick:
        {
            const char* items[] = { "Left", "Right" };
            int index = (1 == code) ? 1 : 0;
            if (ImGui::Combo(id, &index, items, 2)) { code = index; return true; }
            return false;
        }
        default: return false;
        }
    }
}

void CProjectSettingsWindow::OnCreate()
{
    SetLocalizedTitleKey(EditorLocKeys::WindowProjectSettings);

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
    SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
    if (pm)
    {
        m_editResW = static_cast<int>(pm->GetResolutionWidth());
        m_editResH = static_cast<int>(pm->GetResolutionHeight());
        m_editPPU  = pm->GetPixelsPerUnit();
        m_editDefaultFontFamily = pm->GetDefaultFontFamilyGuid();
        m_editFallbackFontFamilies = pm->GetFallbackFontFamilies();

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

        // 입력 레이어 — 동일 패턴(한 줄당 하나, 위 = 최우선).
        m_editInputLayers = pm->GetInputLayers();
        m_editAudioBuses = pm->GetAudioBuses();
        m_editInputActions = pm->GetInputActions();
        ImGui::Utillity::BuildNameListBuffer(m_editInputLayers, m_inputLayersBuffer);
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
    ImGui::Utillity::VerticalSplitter("##ProjectSettingsSplitter",
        m_splitRatio, bodyAvail, MIN_RATIO, MAX_RATIO, SPLITTER_W);

    // ── 우측 콘텐츠 패널 ───────────────────────────────────────────────
    ImGui::BeginChild("##project_settings_content",
        ImVec2(rightW, bodyAvail.y), true);
    DrawCategoryContent(rightW);
    ImGui::EndChild();

    // ── 하단 Apply/Cancel ─────────────────────────────────────────────
    DrawFooterButtons();
}

void CProjectSettingsWindow::DrawCategoryList(float)
{
    struct CategoryEntry { ECategory Kind; const char* LocKey; };
    static const CategoryEntry kCategories[] = {
        { ECategory::General,      EditorLocKeys::ProjectSettingsCategoryGeneral       },
        { ECategory::Script,       EditorLocKeys::ProjectSettingsCategoryScript        },
        { ECategory::Input,        EditorLocKeys::ProjectSettingsCategoryInput         },
        { ECategory::Localization, EditorLocKeys::ProjectSettingsCategoryLocalization  },
        { ECategory::Audio,        EditorLocKeys::ProjectSettingsCategoryAudio         },
        { ECategory::Fonts,        EditorLocKeys::ProjectSettingsCategoryFonts         },
        { ECategory::Debug,        EditorLocKeys::ProjectSettingsCategoryDebug         },
        { ECategory::AssetWatcher, EditorLocKeys::ProjectSettingsCategoryAssetWatcher },
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
    case ECategory::Input:        DrawCategoryInput();        break;
    case ECategory::Localization: DrawCategoryLocalization(); break;
    case ECategory::Audio:        DrawCategoryAudio();        break;
    case ECategory::Fonts:        DrawCategoryFonts();        break;
    case ECategory::Debug:        DrawCategoryDebug();        break;
    case ECategory::AssetWatcher: DrawCategoryAssetWatcher(); break;
    default: break;
    }
}

void CProjectSettingsWindow::DrawCategoryGeneral()
{
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsResolution)).Draw();
    {
        ImGui::Utillity::FormLayout layout("##ps_general_resolution", 4.0f, {2.0f, 1.0f}, 120.0f);
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsWidthPxDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsWidthPx));
            },
            [&]() { ImGui::InputInt("##ps.width_px", &m_editResW); });
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsHeightPxDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsHeightPx));
            },
            [&]() { ImGui::InputInt("##ps.height_px", &m_editResH); });
    }
    if (m_editResW < 1) m_editResW = 1;
    if (m_editResH < 1) m_editResH = 1;

    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsCoordinates)).SpacingBefore(true).Draw();
    {
        ImGui::Utillity::FormLayout layout("##ps_general_coords", 4.0f, {2.0f, 1.0f}, 120.0f);
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsPixelsPerUnitDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsPixelsPerUnit));
            },
            [&]() { ImGui::DragFloat("##ps.ppu", &m_editPPU, 1.0f, 1.0f, 10000.0f, "%.1f"); });
    }
    if (m_editPPU < 1.0f) m_editPPU = 1.0f;
    ImGui::TextDisabled(Loc::Text(EditorLocKeys::ProjectSettingsPpuHelp),
        m_editPPU, 1.0f / m_editPPU);
}

void CProjectSettingsWindow::DrawCategoryScript()
{
    SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();

    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsScript)).Draw();

    {
        ImGui::Utillity::FormLayout layout("##ps_script_form", 4.0f, {2.0f, 1.0f}, 140.0f);

        // 사용자 스크립트 경로 (읽기 전용 디스플레이)
        if (pm)
        {
            const std::string scriptPath = pm->GetScriptPath().generic_string();
            layout.Row(
                [&]() {
                    ImText label;
                    label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsUserScriptsDesc));
                    label(Loc::Text(EditorLocKeys::ProjectSettingsUserScripts));
                },
                [&]() { ImGui::TextDisabled("%s", scriptPath.c_str()); });
        }

        // 빌드 구성
        const char* scriptConfigs[] = { "Debug", "Release" };
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsScriptBuildDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsScriptBuild));
            },
            [&]() { ImGui::Combo("##ps.script_build", &m_scriptBuildConfiguration,
                scriptConfigs, IM_ARRAYSIZE(scriptConfigs)); });

        // 라이브 컴파일 상태
        const ELiveCompileState liveState = pm ? pm->GetLiveCompileState() : ELiveCompileState::Idle;
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsScriptStateDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsScriptState));
            },
            [&]() {
                ImStatusBadge(ToScriptStateText(liveState))
                    .Severity(ToScriptStateSeverity(liveState))
                    .Tooltip(Loc::Text(EditorLocKeys::ProjectSettingsScriptStateDesc))
                    .Draw();
            });

        // 자동 리빌드
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsScriptAutoRebuildDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsScriptAutoRebuild));
            },
            [&]() {
                const bool saveBlocked = false == EditorSimulationGuard::CanSaveProject();
                if (saveBlocked)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Checkbox("##ps.script_auto_rebuild", &m_scriptAutoRebuildEnabled))
                {
                    if (pm)
                    {
                        pm->SetScriptAutoRebuildEnabled(m_scriptAutoRebuildEnabled);
                        std::string error;
                        if (false == pm->SaveProject(&error))
                        {
                            m_errorMessage = false == error.empty() ? error : Loc::Text(EditorLocKeys::ProjectSettingsSaveFailed);
                        }
                        else
                        {
                            m_errorMessage.clear();
                        }
                    }
                }
                if (saveBlocked)
                {
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("%s", EditorSimulationGuard::GetSaveBlockedMessage());
                    }
                    ImGui::EndDisabled();
                }
            });
    }

    ImGui::Spacing();
    const bool buildBlocked = false == EditorSimulationGuard::CanBuildProject();
    if (ImActionButton(Loc::Text(EditorLocKeys::ProjectSettingsScriptRebuild))
        .Tooltip(buildBlocked
            ? EditorSimulationGuard::GetBuildBlockedMessage()
            : Loc::Text(EditorLocKeys::ProjectSettingsScriptRebuildDesc))
        .Disabled(buildBlocked)
        .Draw())
    {
        if (pm)
        {
            pm->SetScriptBuildConfiguration(1 == m_scriptBuildConfiguration
                ? EScriptBuildConfiguration::Release : EScriptBuildConfiguration::Debug);
            pm->RebuildScriptModule();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(Loc::Text(EditorLocKeys::CommonUnload)))
    {
        if (pm) pm->StopLiveCompile();
    }
    ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ProjectSettingsScriptUnloadDesc));
}

void CProjectSettingsWindow::DrawCategoryInput()
{
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsInputTitle)).Draw();
    ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::ProjectSettingsInputDesc));
    ImGui::Spacing();

    // 백킹 버퍼는 멤버(m_inputLayersBuffer) — OnShow 에서 레이어 벡터로부터 재구축됨.
    // 편집 시 벡터로 재파싱. 한 줄 = 한 레이어, 위 = 최우선.
    ImGui::Utillity::NameListEditor("##ps.input.layers", m_inputLayersBuffer, m_editInputLayers);

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::ProjectSettingsInputHint));

    // ── 액션 맵 (이름 기반 액션 → 바인딩) ─────────────────────────────────────────
    ImGui::Spacing();
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsInputActionsTitle)).SpacingBefore(true).Draw();
    ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::ProjectSettingsInputActionsDesc));
    ImGui::Spacing();

    int removeAction       = -1;
    int removeBindingOwner = -1;
    int removeBindingIndex = -1;

    for (int a = 0; a < static_cast<int>(m_editInputActions.size()); ++a)
    {
        InputActionDef& action = m_editInputActions[static_cast<std::size_t>(a)];
        ImGui::PushID(a);

        const std::string header = (action.Name.empty() ? std::string("<unnamed>") : action.Name)
            + "  [" + std::string(magic_enum::enum_name(action.Type)) + "]###action";
        const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (open)
        {
            // 이름 (char 버퍼 시드 후 변경 시 반영).
            char nameBuf[64] = {};
            const std::size_t n = std::min(action.Name.size(), sizeof(nameBuf) - 1);
            std::memcpy(nameBuf, action.Name.data(), n);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
            {
                action.Name = nameBuf;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(Loc::Text(EditorLocKeys::ProjectSettingsInputActionType));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImEnumCombo<EInputActionValueType>("##type", action.Type).Draw();
            ImGui::SameLine();
            if (ImGui::SmallButton(Loc::Text(EditorLocKeys::ProjectSettingsInputRemoveAction)))
            {
                removeAction = a;
            }

            // 바인딩 목록.
            ImGui::Indent();
            for (int b = 0; b < static_cast<int>(action.Bindings.size()); ++b)
            {
                InputBinding& binding = action.Bindings[static_cast<std::size_t>(b)];
                ImGui::PushID(b);

                ImGui::SetNextItemWidth(130.0f);
                ImEnumCombo<EInputBindingSource>("##src", binding.Source).Draw();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                BindingCodeCombo("##code", binding.Source, binding.Code);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::InputInt("##pad", &binding.GamepadIndex, 0, 0); // -1 = 아무 패드
                ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ProjectSettingsInputGamepadIndex));
                if (EInputActionValueType::Vector2 == action.Type)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    ImEnumCombo<EInputComposite>("##comp", binding.Composite).Draw();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                {
                    removeBindingOwner = a;
                    removeBindingIndex = b;
                }

                ImGui::PopID();
            }
            if (ImGui::SmallButton(Loc::Text(EditorLocKeys::ProjectSettingsInputAddBinding)))
            {
                action.Bindings.push_back(InputBinding{});
            }
            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    if (ImGui::Button(Loc::Text(EditorLocKeys::ProjectSettingsInputAddAction)))
    {
        InputActionDef def;
        def.Name = "NewAction";
        m_editInputActions.push_back(std::move(def));
    }

    // 지연 삭제(반복 후).
    if (removeBindingOwner >= 0 && removeBindingOwner < static_cast<int>(m_editInputActions.size()))
    {
        std::vector<InputBinding>& bindings = m_editInputActions[static_cast<std::size_t>(removeBindingOwner)].Bindings;
        if (removeBindingIndex >= 0 && removeBindingIndex < static_cast<int>(bindings.size()))
        {
            bindings.erase(bindings.begin() + removeBindingIndex);
        }
    }
    if (removeAction >= 0 && removeAction < static_cast<int>(m_editInputActions.size()))
    {
        m_editInputActions.erase(m_editInputActions.begin() + removeAction);
    }
}

void CProjectSettingsWindow::DrawCategoryLocalization()
{
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsLocalization)).Draw();

    if (false == Engine.Localization.IsValid())
    {
        ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::ProjectSettingsLocalizationUnavailable));
        return;
    }

    const std::vector<LocalizationLocaleInfo>& locales = Engine.Localization->GetSupportedLocales();
    if (locales.empty())
    {
        ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::ProjectSettingsLocalizationNoLocales));
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
            label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsLanguageDesc));
            label(Loc::Text(EditorLocKeys::ProjectSettingsLanguage));
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
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsAudio)).Draw();

    {
        ImGui::Utillity::FormLayout layout("##ps_audio_form", 4.0f, {2.0f, 1.0f}, 140.0f);
        layout.Row(
            [&]() {
                ImText label;
                label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsAudioMasterVolumeDesc));
                label(Loc::Text(EditorLocKeys::ProjectSettingsAudioMasterVolume));
            },
            // 음량은 범위가 정해진 값이라 슬라이더가 맞다 — 현재 위치가 한눈에 보이고
            // 아무 데나 집어 바로 그 값으로 갈 수 있다. DragFloat 은 범위가 열린 값
            // (좌표·크기 등)에서 상대적으로 밀 때 쓰는 컨트롤이다.
            [&]() { ImGui::SliderFloat("##ps.master_volume", &m_masterVolume, 0.0f, 2.0f, "%.2f"); });
    }

    // ── 믹싱 버스 ────────────────────────────────────────────────────────────
    // 버스 하나 = 목록 한 행(이름 + 기본 음량). AudioPlayer 컴포넌트의 Bus 가 여기 적은
    // 이름을 가리킨다. "Master" 는 항상 존재하므로 적지 않아도 된다.
    ImGui::Spacing();
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsAudioBusesTitle)).SpacingBefore(true).Draw();
    ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::ProjectSettingsAudioBusesDesc));
    ImGui::Spacing();

    ImList<AudioBusDef>(
        "##ps.audio.buses", m_editAudioBuses,
        [&](AudioBusDef& bus, int /*index*/)
        {
            // 폭은 **행 콜백 안에서** 잰다 — ImList 가 핸들·삭제 버튼을 뺀 폭을 여기서
            // PushItemWidth 로 밀어 넣는다. 밖에서 재면 그 둘만큼 넘쳐 버튼이 잘린다.
            const float rowWidth = ImGui::CalcItemWidth();
            const float spacing  = ImGui::GetStyle().ItemSpacing.x;
            // 이름은 짧아도 읽히지만 슬라이더는 좁으면 조작이 안 된다 — 절반 넘게 준다.
            // 패널이 아주 넓어져도 슬라이더만 끝없이 늘어나지 않게 상한을 둔다.
            const float volumeWidth = std::clamp(rowWidth * 0.55f, 90.0f, 220.0f);
            const float nameWidth   = rowWidth - volumeWidth - spacing;

            ImGui::SetNextItemWidth(nameWidth > 40.0f ? nameWidth : rowWidth * 0.4f);
            ImInputText nameInput("##bus_name");
            nameInput.SetText(bus.Name);
            nameInput.SetHintText(Loc::Text(EditorLocKeys::ProjectSettingsAudioBusNameHint));
            if (nameInput(ImGuiInputTextFlags_None))
            {
                bus.Name = static_cast<const char*>(nameInput);
            }

            ImGui::SameLine(0.0f, spacing);
            ImGui::SetNextItemWidth(volumeWidth);
            ImGui::SliderFloat("##bus_volume", &bus.Volume, 0.0f, 2.0f, "%.2f");
            ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ProjectSettingsAudioBusVolumeDesc));
        },
        AudioBusDef{});

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::ProjectSettingsAudioBusesHelp));
}

void CProjectSettingsWindow::DrawCategoryFonts()
{
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsFonts))
        .Description(Loc::Text(EditorLocKeys::ProjectSettingsFontsDesc))
        .Draw();
    ImGui::TextUnformatted(Loc::Text(EditorLocKeys::ProjectSettingsFontsDefault));
    ImAssetField("##default_font_family", m_editDefaultFontFamily)
        .Type(EAssetType::FontFamily)
        .Draw();

    ImGui::Spacing();
    ImGui::TextUnformatted(Loc::Text(EditorLocKeys::ProjectSettingsFontsFallbacks));
    for (std::size_t index = 0; index < m_editFallbackFontFamilies.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        ImAssetField("##fallback_font", m_editFallbackFontFamilies[index])
            .Type(EAssetType::FontFamily)
            .Draw();
        if (index > 0)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(Loc::Text(EditorLocKeys::CommonMoveUp))) std::swap(m_editFallbackFontFamilies[index], m_editFallbackFontFamilies[index - 1]);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(Loc::Text(EditorLocKeys::CommonRemove)))
        {
            m_editFallbackFontFamilies.erase(m_editFallbackFontFamilies.begin() + static_cast<std::ptrdiff_t>(index));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button(Loc::Text(EditorLocKeys::ProjectSettingsFontsAddFallback)))
    {
        m_editFallbackFontFamilies.push_back(INVALID_ASSET_GUID);
    }
}

void CProjectSettingsWindow::DrawCategoryDebug()
{
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsDebug)).Draw();

    ImGui::Utillity::FormLayout layout("##ps_debug_form", 4.0f, {2.0f, 1.0f}, 140.0f);
    layout.Row(
        [&]() {
            ImText label;
            label.SetHoveredTooltip(Loc::Text(EditorLocKeys::ProjectSettingsDebugModeDesc));
            label(Loc::Text(EditorLocKeys::ProjectSettingsDebugMode));
        },
        [&]() { ImGui::Checkbox("##ps.debug_mode", &m_debugModeEnabled); });
}

void CProjectSettingsWindow::DrawCategoryAssetWatcher()
{
    ImSectionHeader(Loc::Text(EditorLocKeys::ProjectSettingsAssetWatcherTitle)).Draw();
    ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::ProjectSettingsAssetWatcherDesc));
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
    ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::ProjectSettingsAssetWatcherSyntaxHint));
}

void CProjectSettingsWindow::DrawFooterButtons()
{
    SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();

    if (false == m_errorMessage.empty())
    {
        ImValidationMessage(m_errorMessage.c_str(), EImValidationSeverity::Error).Draw();
    }

    const bool applied = ImActionButton(Loc::Text(EditorLocKeys::CommonApply))
        .Severity(EImValidationSeverity::Success)
        .Size({ 100.0f, 0.0f })
        .Tooltip(EditorSimulationGuard::CanSaveProject()
            ? Loc::Text(EditorLocKeys::ProjectSettingsApplyDesc)
            : EditorSimulationGuard::GetSaveBlockedMessage())
        .Disabled(false == EditorSimulationGuard::CanSaveProject())
        .Draw();
    if (applied)
    {
        if (false == EditorSimulationGuard::CanSaveProject())
        {
            m_errorMessage = EditorSimulationGuard::GetSaveBlockedMessage();
            return;
        }

        if (pm)
        {
            pm->SetResolution(static_cast<std::uint32_t>(m_editResW),
                              static_cast<std::uint32_t>(m_editResH));
            pm->SetPixelsPerUnit(m_editPPU);
            pm->SetFontSettings(m_editDefaultFontFamily, m_editFallbackFontFamilies);
            pm->SetScriptBuildConfiguration(1 == m_scriptBuildConfiguration
                ? EScriptBuildConfiguration::Release : EScriptBuildConfiguration::Debug);
            pm->SetScriptAutoRebuildEnabled(m_scriptAutoRebuildEnabled);
            pm->SetDebugModeEnabled(m_debugModeEnabled);
            pm->SetAssetWatchIgnorePatterns(m_editAssetWatchIgnorePatterns);
            pm->SetInputLayers(m_editInputLayers);
            pm->SetAudioBuses(m_editAudioBuses);
            pm->SetInputActions(m_editInputActions);
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
            m_errorMessage = false == error.empty() ? error : Loc::Text(EditorLocKeys::ProjectSettingsSaveFailed);
        }
        else
        {
            m_errorMessage.clear();
            SetVisible(false);
        }
    }
    ImGui::SameLine();
    const bool cancelled = ImActionButton(Loc::Text(EditorLocKeys::CommonCancel))
        .Size({ 100.0f, 0.0f })
        .Tooltip(Loc::Text(EditorLocKeys::ProjectSettingsCancelDesc))
        .Draw();
    if (cancelled)
    {
        SetVisible(false);
    }
}
