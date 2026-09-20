#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Editor/Widget/Gizmo.h>
#include <JBro/RHI/RHI.h>

#include "../Gizmo/GizmoEditing.h"

namespace JBro
{
    class GameObject;

    // **편집 화면**이다(D-130). 기존 엔진의 `CCanvasViewTool` 자리이고, 유니티로 치면 씬 뷰다.
    //
    // 게임 뷰와 역할이 다르다. 게임 뷰는 **게임의 카메라가 보는 것**이고 여기는 **만드는 사람이
    // 보는 것**이다. 그래서 카메라가 따로 있고(팬·줌), 격자와 선택 표시가 얹히고, 기즈모로
    // 끌어 옮기는 것도 여기서만 한다. 게임 뷰에서 편집하면 카메라가 없는 캔버스를 편집할 수 없고,
    // 게임이 카메라를 움직이는 순간 편집 화면이 따라 흔들린다.
    //
    // 그림은 엔진이 그린다. 이 패널은 크기와 카메라를 요청하고 그 텍스처를 붙인 뒤,
    // 그 위에 ImGui 로 겹쳐 그린다.
    class CanvasViewPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Center; }

        // 화면 한가운데가 보는 월드 좌표와 배율이다. 세션에 저장할 값이라 밖에서도 읽고 쓴다.
        void SetCamera(float centerX, float centerY, float orthographicSize);
        float GetCameraX() const { return m_centerX; }
        float GetCameraY() const { return m_centerY; }
        float GetCameraSize() const { return m_orthographicSize; }

    private:
        // 그림이 붙은 화면 사각형과 그때의 카메라다. 겹쳐 그리는 것들이 전부 이것을 쓴다.
        struct ViewRect
        {
            float left = 0.0f;
            float top = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };

        void DrawToolBar();
        // 팬(가운데·오른쪽 끌기)과 줌(휠). 그림 위에 마우스가 있을 때만.
        void HandleCameraInput(const ViewRect& rect, bool hovered);
        void DrawGrid(const ViewRect& rect);
        void DrawSelectionOutlines(const ViewRect& rect);
        void DrawGizmo(const ViewRect& rect);
        // 빈 곳을 누르면 그 자리의 오브젝트를 고른다. 아무것도 없으면 선택을 비운다.
        void HandlePicking(const ViewRect& rect, bool hovered);
        void DrawContextMenu();
        // 고른 것들이 다 보이도록 카메라를 맞춘다. 고른 것이 없으면 캔버스 전체다.
        void FrameSelection();

        // 월드 한 점을 그림 위 화면 점으로. 카메라가 직교라 나눗셈 한 번이다.
        void WorldToScreen(const ViewRect& rect, float worldX, float worldY,
            float& screenX, float& screenY) const;
        void ScreenToWorld(const ViewRect& rect, float screenX, float screenY,
            float& worldX, float& worldY) const;
        // 이 화면 점에 걸리는 오브젝트. 없으면 nullptr 이다. 앞에 그려지는 것이 먼저 잡힌다.
        GameObject* PickAt(const ViewRect& rect, float screenX, float screenY) const;
        // 오브젝트가 화면에서 차지하는 사각형(회전은 무시한 외접 사각형)을 월드로 낸다.
        bool GetWorldBounds(const GameObject& object,
            float& minX, float& minY, float& maxX, float& maxY) const;

        EditorApplication* m_editor = nullptr;

        float m_centerX = 0.0f;
        float m_centerY = 0.0f;
        // 화면 세로 절반이 담는 월드 길이다. 게임 카메라의 `orthographicSize` 와 같은 뜻이다.
        float m_orthographicSize = 5.0f;

        GizmoMode m_gizmoMode = GizmoMode::Translate;
        Widget::GizmoState m_gizmoState;
        GizmoEditing m_editing;
        bool m_showGrid = true;
        // 오른쪽 단추로 끌고 있는 중인가. 끌었으면 놓을 때 맥락 메뉴를 열지 않는다 -
        // 화면을 옮기려던 것이지 메뉴를 부르려던 것이 아니다.
        bool m_panning = false;
        bool m_panMoved = false;
    };
}
