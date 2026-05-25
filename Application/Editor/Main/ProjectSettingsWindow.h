#pragma once

#include <array>
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
    int m_editResW = 1920;
    int m_editResH = 1080;

    // 스크립트 DLL 경로 편집 버퍼
    static constexpr int DLL_PATH_BUF_SIZE = 512;
    std::array<char, DLL_PATH_BUF_SIZE> m_dllPathBuf = {};
};

#endif
