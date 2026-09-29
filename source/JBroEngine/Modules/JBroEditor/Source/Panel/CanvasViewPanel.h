#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/EditorShortcutManager.h>
#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Editor/Gizmo/PolygonEditModel.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Physics2D/Geometry.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>
#include <JBro/Editor/Widget/Gizmo.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>

#include "../Gizmo/GizmoEditing.h"

namespace JBro
{
    namespace Object
    {
        class GameObject;
    }
    struct ComponentMenuContext;
    namespace Component
    {
        class Text2D;
    }

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
        void OnDestroy() override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Center; }

        // 이 프로젝트가 3D 인가. **편집 화면의 조작이 여기서 갈린다** - 평면을 밀고 당기는
        // 것으로는 3D 의 뒤를 볼 수 없어, 3D 는 바라보는 점 둘레를 도는 궤도 카메라다.
        bool Is3D() const;

        // 화면 한가운데가 보는 월드 좌표와 배율이다. 세션에 저장할 값이라 밖에서도 읽고 쓴다.
        // **카메라가 가려는 자리다**(D-252). 그리는 카메라는 여기로 부드럽게 따라가고, 이 셋은 손짓한 그 순간의 값이다.
        // 넣으면 따라가지 않고 곧바로 그 자리다 - 세션을 되살릴 때 날아가는 화면을 보일 까닭이 없다.
        void SetCamera(float centerX, float centerY, float orthographicSize);
        float GetCameraX() const { return m_goalX; }
        float GetCameraY() const { return m_goalY; }
        float GetCameraSize() const { return m_goalSize; }

        // **UI 보기**(D-237). 참이면 화면 레이어만 기준 픽셀 좌표로 보이고 고르며 기준 사각형 안내선을 그린다. 편집 카메라는 보기마다 따로다.
        // 고른 오브젝트의 레이어가 다른 공간이면 보기가 따라 바뀐다.
        bool IsScreenView() const { return m_screenView; }
        void SetScreenView(bool screen);

        // 월드 한 점이 마지막으로 그린 화면(2D)의 어디에 놓였는가. 그린 적이 없거나 3D 면 거짓이다.
        bool ProjectWorldToScreen(float worldX, float worldY, float& screenX, float& screenY) const;

        // **이 오브젝트 안으로 들어간다**(D-254, 기존 `SetFocusContext`). 캔버스 뷰와 계층 창의 두 번 누르기가 같은 길이다.
        // 고르기는 부르는 쪽이 한다 - 캔버스 뷰는 자손까지, 계층 창은 그 줄 하나를 고른다.
        void StepInto(Object::GameObject& object);
        // 지금 들어가 있는 오브젝트다. 뿌리면 nullptr 이다.
        Object::GameObject* GetFocus() const;

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
        void DrawPreviewLocale();
        // 팬(가운데·오른쪽 끌기)과 줌(휠). 그림 위에 마우스가 있을 때만.
        void HandleCameraInput(const ViewRect& rect, bool hovered);
        void DrawGrid(const ViewRect& rect);
        // 3D 의 바닥 격자다(y=0 평면). 선을 토막 내어 투영한다 - 한 선이 카메라 평면을
        // 가로지르면 양 끝만으로는 그릴 수 없기 때문이다.
        void DrawGrid3D(const ViewRect& rect);
        void DrawSelectionOutlines(const ViewRect& rect);
        // 고른 것이 스프라이트면 **그림의 실제 모양**을 두른다(D-149). 아직 재지 못했거나
        // 스프라이트가 아니면 거짓이고, 부르는 쪽이 사각형으로 두른다.
        bool DrawSpriteContour(const ViewRect& rect, Object::GameObject& object, ImU32 color);
        // 콜라이더의 모양을 그린다(D-143). 물리는 눈에 보이지 않아서, 그려 주지 않으면
        // 충돌 칸이 스프라이트와 어긋난 것을 부딪혀 봐야만 안다.
        void DrawColliders(const ViewRect& rect);
        void DrawJoints(const ViewRect& rect);

        // ── 폴리곤 콜라이더 편집(physics-plan §4 의 5, 기존 `CCanvasViewTool` 의 버텍스 편집) ─────────
        //
        // **도구 막대의 "콜라이더 편집" 을 켜고 폴리곤 콜라이더가 있는 오브젝트를 고르면** 버텍스 손잡이가 선다.
        // 기존 엔진은 인스펙터에서 그 컴포넌트의 탭을 연 것으로 켰는데, 이 인스펙터에는 그런 자리가 없다.
        // 켜진 동안은 기즈모를 그리지 않는다 - 손잡이가 오브젝트 한가운데의 기즈모와 겹치면 어느 쪽을 잡는지 모른다.
        // 끌기는 놓을 때 커맨드 하나(`points` 전체의 앞뒤 글자)이고, 변 누르기와 지우기도 하나씩이다.
        struct PolygonPose
        {
            Vector2  center;
            float cosine = 1.0f;
            float sine = 0.0f;
            Vector2  scale{1.0f, 1.0f};
        };
        struct PolygonTarget
        {
            Object::GameObject*            object = nullptr;
            Component::Collider2D* collider = nullptr;
            ComponentAddress       address;
            PolygonPose            pose;
        };
        bool FindPolygonTarget(PolygonTarget& target);
        // `Collider2D` 의 우클릭 메뉴에 서는 "포인트 편집" 이다(D-220). `context.user` 가 이 패널이다.
        static bool DrawEditPointsItem(const ComponentMenuContext& context);
        void DrawPolygonEditor(const ViewRect& rect);
        // 버텍스를 우클릭했으면 그 메뉴를 열고 참이다. 캔버스 메뉴 대신이다.
        bool DrawVertexMenu(const ViewRect& rect);
        Vector2 LocalToScreen(const ViewRect& rect, const PolygonPose& pose, Vector2 offset, Vector2 local) const;
        Vector2 ScreenToLocal(const ViewRect& rect, const PolygonPose& pose, Vector2 offset, Vector2 screen) const;
        // `points` 를 `after` 로 바꾸는 커맨드 하나를 올린다. 쓰기 전 값으로 되돌려 둔 뒤 커맨드가 쓴다.
        void CommitPoints(const ComponentAddress& address, const String& before, const Array<Vector2>& after);
        // 폴리곤의 볼록 조각. 꼭짓점과 크기가 바뀔 때만 다시 나눈다 - 그리기는 매 프레임이다.
        struct PieceCache
        {
            std::uint64_t                   signature = 0;
            Physics2D::PolygonError         error = Physics2D::PolygonError::None;
            Array<Physics2D::ConvexPolygon> pieces;
        };
        const PieceCache& PiecesFor(const Component::Collider2D& collider, Vector2 scale);
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
        // 이 오브젝트와 그 자손을 모두 선택에 더한다(D-253).
        void AddTreeToSelection(Object::GameObject& object);
        void DrawContextMenu(const ViewRect& rect);
        // 고른 것들이 다 보이도록 카메라를 맞춘다. 고른 것이 없으면 캔버스 전체다.
        void FrameSelection();

        // 월드 한 점을 그림 위 화면 점으로. 카메라가 직교라 나눗셈 한 번이다.
        void WorldToScreen(const ViewRect& rect, float worldX, float worldY,
            float& screenX, float& screenY) const;
        void ScreenToWorld(const ViewRect& rect, float screenX, float screenY,
            float& worldX, float& worldY) const;
        // 위의 것을 주어진 카메라로 잰다. 휠 줌은 그리는 카메라가 아니라 **가려는 카메라**에서 마우스 아래 점을 붙잡는다(D-252) -
        // 따라가는 도중에 휠을 거듭 돌리면, 그리는 카메라로 잰 점은 칸마다 다른 곳이다.
        static void ScreenToWorldWith(const ViewRect& rect, float centerX, float centerY, float orthographicSize,
            float screenX, float screenY, float& worldX, float& worldY);
        // 그리는 카메라가 가려는 카메라를 따라간다(D-252, 기존 `CAMERA_SMOOTH_SPEED`). 3D 는 곧바로 맞춘다.
        void FollowCameraGoal(float deltaSeconds);
        // 이 오브젝트와 그 자손이 차지하는 사각형을 늘려 담는다. 트랜스폼이 하나도 없으면 `any` 가 그대로다.
        void IncludeTreeBounds(const Object::GameObject& object,
            float& minX, float& minY, float& maxX, float& maxY, bool& any) const;
        // 두 번 눌러 들어가거나 나온 오브젝트로 카메라가 줌해 간다(D-252, 기존 `FocusOnEntity`).
        void FocusCameraOn(const Object::GameObject& object);
        // 이 화면 점에 걸리는 오브젝트. 없으면 nullptr 이다. 앞에 그려지는 것이 먼저 잡힌다.
        Object::GameObject* PickAt(const ViewRect& rect, float screenX, float screenY) const;

        // ── 들어가 고르기(D-157, 기존 `CCanvasViewEditContext`) ──────────────────
        //
        // **누르면 맨 위 부모를 고른다.** 여러 조각으로 된 오브젝트(몸통·팔·무기)의 한 조각을
        // 눌렀는데 그 조각만 고르면, 오브젝트를 통째로 옮기려는 손짓이 조각 하나만 떼어 낸다.
        // 조각을 고치려면 **두 번 눌러 그 안으로 들어간다** - 그 뒤로는 그 오브젝트의 직계 자식이
        // 고르는 단위다. 빈 곳을 두 번 누르면 한 층 나온다.
        //
        // 지금 들어가 있는 오브젝트다. 없으면(뿌리) nullptr 이다. 번호로 들고 있어 지워져도 안전하다.
        // 마우스가 이 뷰의 그림 위에 있는가(D-179). 기즈모 손잡이에 가려도 참이다.
        bool PointerInView(const ViewRect& rect) const;
        // 한 층 나온다(D-254). 나온 오브젝트를 돌려준다. 뿌리면 아무것도 하지 않고 nullptr 이다.
        Object::GameObject* StepOut();
        // **나올 때 돌아갈 자리를 한 번만 적는다**(D-257, D-254 를 고친다). 들어가기 직전에 지금 층의 카메라를 적고, 그 층으로 나올 때 그리로
        // 돌아가며 버린다 - 뿌리로 나오면 들어가기 전의 뿌리 자리, A 로 나오면 A 안에서 더 들어가기 전의 자리다. 들어갈 때는 늘 오브젝트에 맞춘다.
        void RememberCamera();
        // 적힌 자리로 가고 지운다. 없거나 보기가 다르면 거짓이다(그래도 지운다).
        bool TakeCamera(std::uint64_t context);
        // 누른 오브젝트를 고른다(D-254, 기존 `CollectSubtree`). 들어가 있는 오브젝트 자신이면 그것 하나, 아니면 자손까지다.
        void SelectPicked(Object::GameObject& picked);
        void RemoveTreeFromSelection(Object::GameObject& object);
        // 걸린 오브젝트를 **지금 층의 오브젝트**로 올린다. 뿌리에서는 맨 위 조상, 들어가 있으면
        // 그 오브젝트의 직계 자식(또는 그 오브젝트 자신). 들어간 오브젝트 밖이면 nullptr 이다.
        Object::GameObject* MapToLevel(Object::GameObject* hit) const;
        // 화면 왼쪽 위의 상태 글자(D-172, 기존 캔버스 뷰의 오버레이): 고른 것, 편집 카메라,
        // 그리고 들어가 있으면 그 사실과 나오는 법.
        void DrawOverlay(const ViewRect& rect);
        // 오브젝트가 화면에서 차지하는 사각형(회전은 무시한 외접 사각형)을 월드로 낸다.
        bool GetWorldBounds(const Object::GameObject& object,
            float& minX, float& minY, float& maxX, float& maxY) const;

        EditorApplication* m_editor = nullptr;

        // 마지막으로 그린 화면이다. 겹쳐 그리는 도구와 테스트가 같은 변환을 쓴다.
        ViewRect m_lastRect;
        bool m_hasLastRect = false;

        // 폴리곤 콜라이더 편집.
        bool m_editCollider = false;
        // 우클릭 메뉴의 "포인트 편집" 으로 고른 콜라이더다(D-220). 비어 있으면 고른 오브젝트의 첫 폴리곤이다.
        // 고른 오브젝트가 바뀌거나 그 콜라이더가 사라지면 잊는다.
        ComponentAddress m_pointTarget;
        PolygonEditModel::Hit m_polygonHover;
        bool m_vertexDragging = false;
        bool m_vertexPressed = false;
        std::uint32_t m_dragVertex = 0;
        ComponentAddress m_dragAddress;
        String m_dragBefore;
        Array<Vector2> m_dragPoints;
        ComponentAddress m_menuAddress;
        std::uint32_t m_menuVertex = 0;
        // 프레임마다 다시 쓰는 칸들. 용량이 남아 두 번째 프레임부터는 할당하지 않는다.
        Array<Component::Collider2D*> m_colliderScratch;
        Array<Component::DistanceJoint2D*> m_distanceJointScratch;
        Array<Component::HingeJoint2D*> m_hingeJointScratch;
        Array<Vector2> m_screenScratch;
        Array<Vector2> m_outlineScratch;
        Table<InstanceId, PieceCache> m_pieceCache;

        // **그리는 카메라다.** 그림·격자·고르기·기즈모가 모두 이것으로 센다.
        float m_centerX = 0.0f;
        float m_centerY = 0.0f;
        // 화면 세로 절반이 담는 월드 길이다. 게임 카메라의 `orthographicSize` 와 같은 뜻이다.
        float m_orthographicSize = 5.0f;
        // **가려는 카메라다**(D-252). 팬·줌·맞추기·들어가기는 이것만 바꾸고, 그리는 카메라가 매 프레임 따라온다.
        float m_goalX = 0.0f;
        float m_goalY = 0.0f;
        float m_goalSize = 5.0f;
        // UI 보기(D-237)와, 쉬고 있는 쪽 보기의 편집 카메라다. 보기를 바꾸면 지금 카메라와 맞바꾼다.
        bool m_screenView = false;
        bool m_otherCameraSet = false;
        float m_otherCenterX = 0.0f;
        float m_otherCenterY = 0.0f;
        float m_otherSize = 5.0f;
        // 고른 것이 바뀐 프레임에만 보기를 따라 바꾼다 - 손으로 바꾼 보기를 매 프레임 되돌리지 않는다.
        Object::GameObject* m_lastSelection = nullptr;
        // 이 오브젝트가 지금 보기의 공간에 있는가. 그리기·고르기·테두리가 같은 규칙이다.
        bool InViewSpace(const Object::GameObject& object) const;
        void DrawReferenceRect(const ViewRect& rect);
        // 3D 의 궤도 카메라(D-136). 바라보는 점의 높이와, 그 점에서의 거리와 각이다.
        float m_centerZ = 0.0f;
        float m_distance = 12.0f;
        float m_yawDegrees = 40.0f;
        float m_pitchDegrees = -25.0f;

        GizmoMode m_gizmoMode = GizmoMode::Translate;
        // W·E·R 을 누르면 모드를 바꾸는 할 일(D-228). 패널이 `OnDestroy` 에서 등록을 풀므로 패널보다 오래 살지 않는다.
        class GizmoModeShortcut final : public IEditorShortcutHandler
        {
        public:
            GizmoModeShortcut(CanvasViewPanel& panel, GizmoMode mode);
            bool Execute(EditorApplication& editor) override;

        private:
            CanvasViewPanel& m_panel;
            GizmoMode m_mode;
        };
        ShortcutHandle m_shortcuts[3] = {};
        // 손잡이를 오브젝트의 축에 둘지 월드 축에 둘지(D-171). 크기 모드에서는 쓰지 않는다.
        GizmoSpace m_gizmoSpace = GizmoSpace::Local;
        Widget::GizmoState m_gizmoState;
        GizmoEditing m_editing;
        bool m_showGrid = true;
        // 콜라이더를 보일지. 늘 그리면 그림을 다듬는 동안 녹색 선이 방해가 된다.
        bool m_showColliders = true;
        // **눈금을 픽셀로 읽을지**(D-184, 기존 `단위: Unit`/`단위: Pixel`). 거짓이면 월드 유닛이다.
        // 그림은 픽셀로 그려 오는데 씬은 유닛으로 세므로, 스프라이트를 자리에 맞출 때
        // 그 둘을 머리로 곱하고 있어야 했다.
        //
        // **곱하는 값은 에셋 PPU 의 기본값(100)이다**(사용자 결정). PPU 는 에셋마다 따로 있고
        // (D-117) 프로젝트에는 없으므로, 어느 그림에도 매이지 않은 눈금은 그 기본값으로 읽는다.
        // 기본값이 아닌 PPU 로 들여온 그림에서는 이 숫자가 그 그림의 픽셀과 어긋난다.
        bool m_rulerInPixels = false;
        // 들어가 있는 오브젝트의 번호다(D-157). 0 이면 뿌리다.
        std::uint64_t m_focus = 0;
        // 층(들어간 오브젝트의 번호, 뿌리는 0)마다 **돌아올 자리**다(D-257). 더 들어갈 때 적고 그리로 나올 때 쓰고 버린다.
        // 보기(월드/UI)가 다르면 단위가 달라 쓰지 않는다.
        struct CameraMemo
        {
            float centerX = 0.0f;
            float centerY = 0.0f;
            float size = 5.0f;
            bool screenView = false;
        };
        Table<std::uint64_t, CameraMemo> m_cameraMemos;
        // 이번 누름이 두 번째 누름인가. 누를 때 알고 뗄 때 쓴다 - 고르기는 뗄 때 한다.
        bool m_doubleClick = false;
        // 오른쪽 단추로 끌고 있는 중인가. 끌었으면 놓을 때 맥락 메뉴를 열지 않는다 -
        // 화면을 옮기려던 것이지 메뉴를 부르려던 것이 아니다.
        bool m_panning = false;
        bool m_panMoved = false;
        // 상자 선택 중인가와 그 시작 자리(화면 좌표). 끌기가 임계값을 넘어야 시작한다 -
        // 넘기 전에 시작하면 그냥 클릭한 것도 빈 상자가 되어 선택이 풀린다.
        bool m_boxSelecting = false;
        ImVec2 m_boxStart{0.0f, 0.0f};
        // 오브젝트 하나에 붙은 텍스트들을 모으는 자리다. 외곽선이 매 프레임 묻으므로 한 번 잡은 용량을 계속 쓴다.
        mutable Array<Component::Text2D*> m_textScratch;
    };
}
