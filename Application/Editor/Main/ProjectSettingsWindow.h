#pragma once

#include <string>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

class CProjectSettingsWindow : public CImCustomWindow
{
public:
    using CImCustomWindow::CImCustomWindow;
    virtual ~CProjectSettingsWindow() = default;

private:
    void OnCreate()     override;
    void OnShow()       override;
    void OnRenderStay() override;

    // 편집 중인 임시 값 — [적용] 버튼을 누를 때만 ProjectManager에 반영합니다.
    int   m_editResW = 1920;
    int   m_editResH = 1080;
    float m_editPPU  = 100.0f;
    int   m_selectedLocaleIndex = 0;

    int m_scriptBuildConfiguration = 0;
    bool m_scriptAutoRebuildEnabled = false;
};

#endif
