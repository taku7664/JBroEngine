#pragma once

#include <JBro/Editor/EditorPopup.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // **새 프로젝트의 이름과 프레임워크를 받는다**(D-160, 기존 `CRootDockWindow::RenderNewProjectPopup`).
    // 폴더는 이 팝업을 띄우기 전에 대화상자로 골랐다. 기존은 이름만 받았다 - 우리 프로젝트 파일은
    // 2D 인지 3D 인지를 적어야 열리므로(D-99) 그것도 여기서 고른다.
    // 만들지 못하면 닫지 않고 사유를 그 자리에 보인다 - 이름만 고쳐 다시 누를 수 있게.
    class NewProjectPopup final : public EditorPopup
    {
    public:
        explicit NewProjectPopup(const char* parentFolder);

        const char* GetTitle() const override;
        const char* GetId() const override;
        float GetInitialWidth() const override;
        void OnDraw(EditorApplication& editor) override;

    private:
        String m_parentFolder;
        String m_name;
        String m_error;
        int m_framework = 0;
    };
}
