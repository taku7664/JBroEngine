#include "pch.h"
#include "ProjectSettingsWindow.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

namespace
{
    SafePtr<CProjectManager> GetProjectManager()
    {
        return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
    }
}

void CProjectSettingsWindow::OnCreate()
{
    SetTitle(Utillity::U8(u8"프로젝트 세팅"));

    m_imguiFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize;

    m_windowFlags = IMWINDOW_FLAG_NONE;

    SetSize({ 460.0f, 260.0f });
    SetVisible(false);
}

void CProjectSettingsWindow::OnShow()
{
    // 창이 열릴 때마다 현재 프로젝트 설정값으로 초기화합니다.
    SafePtr<CProjectManager> pm = GetProjectManager();
    if (pm)
    {
        m_editResW = static_cast<int>(pm->GetResolutionWidth());
        m_editResH = static_cast<int>(pm->GetResolutionHeight());

        // DLL 경로 버퍼 초기화
        const std::string& dllPath = pm->GetScriptDllPath();
        std::snprintf(m_dllPathBuf.data(), m_dllPathBuf.size(), "%s", dllPath.c_str());
    }
}

void CProjectSettingsWindow::OnRenderStay()
{
    SafePtr<CProjectManager> pm = GetProjectManager();

    // ── 해상도 섹션 ──────────────────────────────────────────────────
    ImGui::SeparatorText(Utillity::U8(u8"해상도"));

    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputInt(Utillity::U8(u8"너비 (px)"),  &m_editResW);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputInt(Utillity::U8(u8"높이 (px)"), &m_editResH);

    // 0 이하 값 방지
    if (m_editResW < 1) m_editResW = 1;
    if (m_editResH < 1) m_editResH = 1;

    // ── 스크립트 DLL 섹션 ─────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText(Utillity::U8(u8"스크립트 DLL"));

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("##DllPath", m_dllPathBuf.data(), m_dllPathBuf.size());
    ImGui::SameLine();
    if (ImGui::Button(Utillity::U8(u8"찾기")))
    {
        File::Path chosenPath;
        if (File::ShowOpenFileDialog(
            nullptr,
            L"스크립트 DLL 선택",
            L"",
            { { L"Dynamic Library", L"*.dll" }, { L"All Files", L"*.*" } },
            chosenPath))
        {
            const std::string pathStr = chosenPath.string();
            std::snprintf(m_dllPathBuf.data(), m_dllPathBuf.size(), "%s", pathStr.c_str());
        }
    }

    // DLL 로드 상태 표시
    const bool dllLoaded = pm && pm->IsScriptModuleLoaded();
    ImGui::TextColored(
        dllLoaded ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        dllLoaded ? Utillity::U8(u8"● DLL 로드됨") : Utillity::U8(u8"○ DLL 미로드"));

    ImGui::SameLine();
    if (ImGui::Button(Utillity::U8(u8"로드")))
    {
        if (pm)
        {
            pm->SetScriptDllPath(m_dllPathBuf.data());
            const bool ok = pm->LoadScriptModule();
            (void)ok;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Utillity::U8(u8"언로드")))
    {
        if (pm)
        {
            pm->UnloadScriptModule();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 버튼 ──────────────────────────────────────────────────────────
    if (ImGui::Button(Utillity::U8(u8"적용"), { 100.0f, 0.0f }))
    {
        if (pm)
        {
            // 값만 메모리에 반영. 실제 파일 저장은 "프로젝트 저장" 메뉴에서 수행.
            pm->SetResolution(
                static_cast<std::uint32_t>(m_editResW),
                static_cast<std::uint32_t>(m_editResH));
            pm->SetScriptDllPath(m_dllPathBuf.data());
        }
        SetVisible(false);
    }

    ImGui::SameLine();

    if (ImGui::Button(Utillity::U8(u8"취소"), { 100.0f, 0.0f }))
    {
        SetVisible(false);
    }
}
