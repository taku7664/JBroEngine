#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/RHI/RHI.h>

namespace JBro
{
    // 게임 화면을 보여 주는 패널이다. 게임은 에디터가 잡아 둔 텍스처에 그려지고
    // 여기서는 그것을 붙이기만 한다(D-63).
    class GameViewPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;

    private:
        EditorApplication* m_editor = nullptr;
    };
}
