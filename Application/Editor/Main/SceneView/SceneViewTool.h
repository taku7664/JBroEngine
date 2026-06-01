#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"
#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Utillity/Math/Vector2T.h"

#include "SceneViewEditContext.h"

class CScene;

class CSceneViewTool : public CImWindow
{
public:
    using CImWindow::CImWindow;
    virtual ~CSceneViewTool() = default;

    // 에디터 카메라 상태 getter (저장용: target 값 반환)
    Vector2 GetEditorCameraPos()  const { return m_targetCameraPos; }
    float          GetEditorCameraSize() const { return m_targetCameraSize; }

    // 에디터 카메라 즉시 이동 (프로젝트 로드 시 적용)
    void SetEditorCamera(float x, float y, float size);

    // 지정 엔티티를 화면 중앙에 포커싱 (카메라 이동만)
    void FocusOnEntity(EntityId entity, const CScene& scene);

    // 하이어라키 더블클릭용: 편집 컨텍스트를 entity로 전환 + 카메라 이동
    // 씬뷰 내 더블클릭과 동일하게 m_editCtx 를 갱신하므로,
    // 이후 씬뷰 클릭은 entity 컨텍스트(직계 자식) 기준으로 동작함
    void SetFocusContext(EntityId entity, const CScene& scene);

    // Flash-like 포커스 컨텍스트 초기화 (씬 변경, 프로젝트 닫기 시 호출)
    void ClearEditContext();

private:
    void OnCreate()     override;
    void OnDestroy()    override;
    void OnUpdate()     override;
    void OnRenderStay() override;

private:
    // ── 에디터 카메라 ─────────────────────────────────────────────────────────
    // target: 입력으로 즉시 수정, display: target 을 향해 부드럽게 보간.
    // CAMERA_SMOOTH_SPEED 는 SceneViewTool.cpp 에서 조절합니다.
    Vector2 m_targetCameraPos  = Vector2(0.0f, 0.0f);
    float          m_targetCameraSize = 5.0f;
    Vector2 m_cameraPos        = Vector2(0.0f, 0.0f);
    float          m_cameraSize       = 5.0f;

    // ── Flash-like 포커스 내비게이션 ────────────────────────────────────────
    CSceneViewEditContext m_editCtx;

    // ── 그리드 눈금 단위 표시 ─────────────────────────────────────────────────
    bool m_showPixelGrid = false; // false = Unit 모드, true = Pixel 모드

    // ── 드래그 박스 선택 상태 ─────────────────────────────────────────────────
    bool   m_isDraggingLeft      = false; // 드래그 중
    bool   m_clickPending        = false; // 클릭 대기 (드래그로 전환되면 취소됨)
    bool   m_clickPendingDouble  = false; // 대기 중인 클릭이 더블클릭인지
    ImVec2 m_dragStartScrn       = {};    // 드래그 시작 뷰포트 좌표
    ImVec2 m_dragCurrentScrn     = {};    // 드래그 현재 뷰포트 좌표

    // ── 폴리곤 버텍스 드래그 상태 ────────────────────────────────────────────
    // Layer 2.8: 버텍스 핸들을 드래그해 위치를 편집한다.
    EntityId               m_dragPolyEntity      = INVALID_ENTITY_ID;
    int                    m_dragVertexIndex      = -1;
    std::vector<Vector2> m_dragPreviewPts; // 드래그 중 미리보기 포인트
    std::vector<Vector2> m_dragOldPts;     // 드래그 시작 시점 스냅샷 (Undo용)

    // ── 엣지/버텍스 클릭 소비 ─────────────────────────────────────────────────
    // Layer 2.8 에서 핸들 조작이 처리됐을 때 입력 블록의 씬 선택을 억제한다.
    bool m_suppressNextClick = false;

    // ── 버텍스 삭제 팝업 ─────────────────────────────────────────────────────
    EntityId m_deleteVtxEntity = INVALID_ENTITY_ID;
    int      m_deleteVtxIndex  = -1;

    // ── 컨텍스트 메뉴 ────────────────────────────────────────────────────────
    // 우클릭(드래그 아님) 시 피킹된 엔티티. INVALID = 빈 공간 클릭.
    EntityId m_contextMenuEntity  = INVALID_ENTITY_ID;
    // "Add Object" 생성 시 사용할 부모. 빈 공간 + 포커스 중이면 포커스 엔티티.
    EntityId m_contextMenuParent  = INVALID_ENTITY_ID;
    bool     m_rightClickPending  = false; // 우클릭 대기 (드래그로 전환되면 취소됨)
    bool     m_rightDragging      = false; // 우클릭 드래그(팬) 중
};
