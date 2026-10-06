#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Types/Bool.h>

namespace JBro
{
    // **스크립트 빌드의 오류와 경고를 줄마다 보인다**(cpp-script-plan §3.4, D-299, 사용자 결정 2026-09-29: "새 '빌드 결과' 패널").
    // 줄을 누르면 에디터 설정에서 고른 편집기로 그 파일의 그 줄을 연다. 같은 줄은 로그에도 남는다.
    class BuildResultsPanel final : public UniquePanel
    {
    public:
        // 패널 종류 이름이다(D-284). 종류 표와 `GetTitle` 이 같은 글자를 쓴다.
        static constexpr const char* TypeName = "BuildResults";

        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        EditorApplication* m_editor = nullptr;
    };
}
