#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/ECS/EntityTypes.h"

class CScene;

namespace EditorGuiDrawHelpers
{
	// ── 컴포넌트 추가 ──────────────────────────────────────────────────────────

	// 팝업 메뉴 항목으로 컴포넌트 카테고리 서브메뉴를 표시.
	// ImGui::BeginPopupContextItem/ContextWindow 안에서 호출할 것.
	bool DrawAddComponentMenu(CScene& scene, EntityId entity);

	// 버튼 + 팝업 조합으로 컴포넌트 추가 UI 표시.
	bool DrawAddComponentButton(CScene& scene, EntityId entity);

	// ── 오브젝트 추가 ──────────────────────────────────────────────────────────

	// "Add Object" / "Add Child Object" MenuItem 하나를 표시.
	//   parent == INVALID_ENTITY_ID : 루트에 오브젝트 추가 → 레이블 "Add Object"
	//   parent != INVALID_ENTITY_ID : parent의 자식으로 추가 → 레이블 "Add Child Object"
	// 성공 시 생성된 엔티티를 선택 상태로 만들고 true 반환.
	bool DrawAddObjectMenu(CScene& scene, EntityId parent);
}

#endif
