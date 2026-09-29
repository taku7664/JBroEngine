#pragma once

#include <JBro/Editor/EditorPanel.h>

namespace JBro
{
    // **스크립트 빌드의 오류와 경고를 줄마다 보인다**(cpp-script-plan §3.4, D-267, 사용자 결정 2026-09-29: "새 '빌드 결과' 패널").
    // 줄을 누르면 에디터 설정에서 고른 편집기로 그 파일의 그 줄을 연다. 같은 줄은 로그에도 남는다.
    class BuildResultsPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        EditorApplication* m_editor = nullptr;
    };
}
