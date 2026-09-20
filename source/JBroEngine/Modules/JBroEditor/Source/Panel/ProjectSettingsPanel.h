#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // 프로젝트 설정을 보고 고친다(D-137). 기존 엔진 `CProjectSettingsWindow` 자리다.
    //
    // **`.jproject` 를 직접 고친다.** 캔버스의 값이 아니므로 되돌리기 스택에 올리지 않는다 -
    // 에디터의 Ctrl+Z 는 씬 편집을 되돌리는 것이고, 프로젝트 설정은 파일에 적히는 순간
    // 그 파일이 정본이다. 대신 **저장을 누르기 전까지는 파일에 손대지 않는다.**
    //
    // 쓰기는 원문을 타고 가며 아는 키의 값만 바꾼다. 주석도, 우리가 모르는 키도,
    // 시퀀스도 그 자리에 남는다.
    //
    // 처음에는 닫혀 있다. 설정 창은 찾아 여는 창이다.
    class ProjectSettingsPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }

    private:
        // 파일에서 읽은 값을 편집본으로 옮긴다. 창을 열 때와 저장한 뒤에 부른다.
        void Reload();

        EditorApplication* m_editor = nullptr;
        // 편집본이다. 저장을 누를 때까지 파일에 가지 않는다.
        ProjectFile m_draft;
        // 어느 프로젝트의 값을 담고 있는가. 프로젝트가 바뀌면 다시 읽는다.
        String m_loadedPath;
        bool m_loaded = false;
        // 저장한 뒤 남기는 한 줄. 성공과 실패를 같은 자리에서 말한다.
        String m_message;
        bool m_messageIsError = false;
    };
}
