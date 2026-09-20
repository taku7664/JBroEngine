#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/RHI/RHI.h>

namespace JBro
{
    // **시뮬레이션 화면**이다. 게임의 카메라가 보는 것을 그대로 보여 준다.
    //
    // 여기서는 **편집하지 않는다**(D-131). 기즈모도 피킹도 없다 - 편집은 캔버스 뷰의 일이고,
    // 그쪽은 편집 카메라를 따로 가진다(D-130). 기존 엔진의 `CGameViewTool` 도 그림을 붙이고
    // 상태만 얹었다. 게임 뷰에서 편집하게 두면 카메라가 없는 캔버스를 편집할 수 없고,
    // 게임이 카메라를 움직이는 순간 편집 화면이 따라 흔들린다.
    class GameViewPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;

    private:
        // 그림 왼쪽 위에 상태를 적는다. 재생 중인지 정지인지, 그릴 것이 없으면 왜인지.
        void DrawStatusOverlay(float left, float top, bool hasImage) const;

        EditorApplication* m_editor = nullptr;
    };
}
