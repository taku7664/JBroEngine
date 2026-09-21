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

        // 이 프로젝트가 3D 인가. **편집 화면의 조작이 여기서 갈린다** - 평면을 밀고 당기는
        // 것으로는 3D 의 뒤를 볼 수 없어, 3D 는 바라보는 점 둘레를 도는 궤도 카메라다.
        bool Is3D() const;

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
            // **엔진이 그린 화면의 크기다**(D-150). 패널의 크기가 아니다 - 편집 화면 텍스처는
            // 64 의 배수로 올려 잡히고(`RequestCanvasView`), 엔진은 **그 텍스처 전체**를
            // 화면으로 보고 그린다. 패널은 그중 왼쪽 위만 잘라 붙인다.
            //
            // 겹쳐 그리는 것들이 패널 크기로 좌표를 세면, 월드 원점이 패널 한가운데라고
            // 여기게 된다. 실제로는 **텍스처 한가운데**라, 그 차이(보통 수십 픽셀)만큼
            // 격자도 테두리도 기즈모도 그림과 어긋난다.
            float drawWidth = 0.0f;
            float drawHeight = 0.0f;
        };

        void DrawToolBar();
        // 팬(가운데·오른쪽 끌기)과 줌(휠). 그림 위에 마우스가 있을 때만.
        void HandleCameraInput(const ViewRect& rect, bool hovered);
        void DrawGrid(const ViewRect& rect);
        // 3D 의 바닥 격자다(y=0 평면). 선을 토막 내어 투영한다 - 한 선이 카메라 평면을
        // 가로지르면 양 끝만으로는 그릴 수 없기 때문이다.
        void DrawGrid3D(const ViewRect& rect);
        void DrawSelectionOutlines(const ViewRect& rect);
        // 고른 것이 스프라이트면 **그림의 실제 모양**을 두른다(D-149). 아직 재지 못했거나
        // 스프라이트가 아니면 거짓이고, 부르는 쪽이 사각형으로 두른다.
        bool DrawSpriteContour(const ViewRect& rect, GameObject& object, ImU32 color);
        // 콜라이더의 모양을 그린다(D-143). 물리는 눈에 보이지 않아서, 그려 주지 않으면
        // 충돌 칸이 스프라이트와 어긋난 것을 부딪혀 봐야만 안다.
        void DrawColliders(const ViewRect& rect);
        void DrawGizmo(const ViewRect& rect);
        // 화면과 월드를 잇는 카메라를 만든다. 2D 는 우리가 아는 직교 행렬로, 3D 는
        // **렌더러가 이번 프레임에 실제로 쓴 편집 카메라**로 만든다(D-140) - 여기서 같은
        // 행렬을 한 번 더 세우면 둘로 갈려 손잡이가 그림과 다른 자리에 선다.
        bool MakeGizmoCamera(const ViewRect& rect, GizmoCamera& camera) const;
        // 3D 의 고르기와 표시. 평면 좌표에 기대지 않고 **그린 카메라의 투영**을 거친다.
        void DrawSelectionMarkers3D(const ViewRect& rect);
        void HandlePicking3D(const ViewRect& rect, bool hovered);
        // 빈 곳을 누르면 그 자리의 오브젝트를 고른다. 아무것도 없으면 선택을 비운다.
        void HandlePicking(const ViewRect& rect, bool hovered);
        // 빈 곳에서 끌면 상자가 따라오고, 놓으면 그 안에 **닿은** 것을 모두 고른다
        // (기존 엔진 `CCanvasViewTool` 의 드래그 박스 선택). 상자를 그리는 것도 여기다.
        void HandleBoxSelect(const ViewRect& rect, bool hovered);
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
        // 3D 의 궤도 카메라(D-136). 바라보는 점의 높이와, 그 점에서의 거리와 각이다.
        float m_centerZ = 0.0f;
        float m_distance = 12.0f;
        float m_yawDegrees = 40.0f;
        float m_pitchDegrees = -25.0f;

        GizmoMode m_gizmoMode = GizmoMode::Translate;
        Widget::GizmoState m_gizmoState;
        GizmoEditing m_editing;
        bool m_showGrid = true;
        // 콜라이더를 보일지. 늘 그리면 그림을 다듬는 동안 녹색 선이 방해가 된다.
        bool m_showColliders = true;
        // 오른쪽 단추로 끌고 있는 중인가. 끌었으면 놓을 때 맥락 메뉴를 열지 않는다 -
        // 화면을 옮기려던 것이지 메뉴를 부르려던 것이 아니다.
        bool m_panning = false;
        bool m_panMoved = false;
        // 상자 선택 중인가와 그 시작 자리(화면 좌표). 끌기가 임계값을 넘어야 시작한다 -
        // 넘기 전에 시작하면 그냥 클릭한 것도 빈 상자가 되어 선택이 풀린다.
        bool m_boxSelecting = false;
        ImVec2 m_boxStart{0.0f, 0.0f};
    };
}
