#pragma once

#include "Engine/Editor/Project/ProjectTypes.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/ImWindow/ImCustomWindow.h"

#include <string>

class CProjectSettingsWindow : public CImCustomWindow
{
public:
    using CImCustomWindow::CImCustomWindow;
    virtual ~CProjectSettingsWindow() = default;

    // ── 카테고리 ─────────────────────────────────────────────────────────
    enum class ECategory
    {
        General,        // 해상도, 좌표계(PPU)
        Script,         // 빌드 구성, 자동 리빌드
        Localization,   // 언어
        Audio,          // 마스터 볼륨, 버스 (향후 확장)
        Count
    };

private:
    void OnCreate()     override;
    void OnShow()       override;
    void OnRenderStay() override;

    // 좌측 카테고리 리스트 + 우측 패널 그리기
    void DrawCategoryList(float panelWidth);
    void DrawCategoryContent(float panelWidth);

    // 카테고리별 우측 패널 콘텐츠
    void DrawCategoryGeneral();
    void DrawCategoryScript();
    void DrawCategoryLocalization();
    void DrawCategoryAudio();

    // 하단 Apply / Cancel
    void DrawFooterButtons();

    // ── 편집 중 값 — [적용] 시 ProjectManager 반영 ──────────────────────
    int   m_editResW = 1920;
    int   m_editResH = 1080;
    float m_editPPU  = 100.0f;
    int   m_selectedLocaleIndex = 0;

    int   m_scriptBuildConfiguration = 0;
    bool  m_scriptAutoRebuildEnabled = false;
    std::string m_errorMessage;

    // 오디오 (향후 PR D 의 CAudioService 와 연동)
    float m_masterVolume = 1.0f;

    // UI 상태
    ECategory m_selectedCategory = ECategory::General;
    float     m_splitRatio       = 0.28f;   // 왼쪽 패널 너비 비율
};

#endif
