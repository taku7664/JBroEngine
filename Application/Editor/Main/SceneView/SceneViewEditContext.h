#pragma once

#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Utillity/Vector2T.h"
#include <vector>

class CScene;
class IAssetManager;
struct ImDrawList;
struct ImVec2;

// ── CSceneViewEditContext ─────────────────────────────────────────────────────
//
//  포커스 내비게이션 상태 및 오버레이 렌더링 담당.
//
//  루트 모드  (m_context == INVALID_ENTITY_ID) : 씬 최상위 레벨 탐색
//  포커스 모드 (m_context 유효)               : 해당 엔티티 "안" 에서 직계 자식 탐색
//
//  단일 클릭               → 현재 레벨에서 엔티티 선택 (컨텍스트 변경 없음)
//  더블 클릭 on 오브젝트   → 자식 있는 경우 진입 + 카메라 포커싱
//  더블 클릭 on 빈 공간   → 상위 컨텍스트로 탈출

class CSceneViewEditContext
{
public:
    EntityId GetContext() const { return m_context; }
    bool     IsActive()   const { return m_context != INVALID_ENTITY_ID; }

    void Clear() { m_context = INVALID_ENTITY_ID; }

    // m_context 가 이미 죽은 엔티티면 INVALID 로 초기화.
    void Validate(const CScene& scene);

    // 현재 컨텍스트 레벨에서 worldPt 에 가장 위에 있는 엔티티 반환.
    //  루트 모드 → 루트 조상 엔티티
    //  포커스 모드 → m_context 의 직계 자식 엔티티
    //  없으면 INVALID_ENTITY_ID
    EntityId Pick(
        const CScene& scene,
        const Vector2<float>& worldPt,
        IAssetManager* assetMgr) const;

    // 드래그 박스 선택: [worldMin, worldMax] 안에 오브젝트가 "완전히" 포함된
    // 엔티티들을 현재 컨텍스트 레벨로 매핑하여 반환 (중복 없음).
    //  - 스프라이트 있음 → 불투명 픽셀 tight AABB 기준 (assetMgr 없으면 OBB)
    //  - 스프라이트 없음 → 1×1 단위 OBB (Transform 기준)
    //  루트 모드 → 루트 조상 엔티티
    //  포커스 모드 → m_context 의 직계 자식 (또는 m_context 자신)
    std::vector<EntityId> PickBox(
        const CScene& scene,
        const Vector2<float>& worldMin,
        const Vector2<float>& worldMax,
        IAssetManager* assetMgr = nullptr) const;

    // 더블 클릭 on 오브젝트:
    //   - m_context = picked (자식 유무 무관, 항상 진입)
    //   - 선택(Editor::SelectEntities)은 호출자(SceneViewTool)가 담당
    //   - 반환값 = 카메라가 포커싱할 엔티티 (항상 picked 반환)
    EntityId OnDoubleClick(const CScene& scene, EntityId picked);

    // 더블 클릭 on 빈 공간:
    //   - m_context = m_context 의 부모 (루트면 INVALID)
    //   - 반환값 = 탈출한 엔티티 (호출자가 SelectEntities + FocusOnEntity에 사용)
    //             루트에서 탈출할 게 없으면 INVALID_ENTITY_ID
    EntityId OnDoubleClickEmpty(const CScene& scene);

private:
    EntityId m_context = INVALID_ENTITY_ID;
};
