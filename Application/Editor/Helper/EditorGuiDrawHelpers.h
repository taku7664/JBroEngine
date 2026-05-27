#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/ECS/EntityTypes.h"

#include <string>

class CScene;
struct ComponentTypeInfo;
struct ReflectPropertyInfo;
struct ScriptTypeInfo;

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

	// ── 리플렉션 → 로컬라이즈 라벨 ─────────────────────────────────────────────
	// 셋 다 키 없으면 fallback(DisplayName → Name) 반환.

	// 스크립트 타입의 표시명. nullptr 입력 시 "inspector.unknown_script" 반환.
	// 반환 포인터는 리플렉션 레지스트리의 정적 문자열이거나 LocalizationManager 내부 버퍼.
	const char* GetScriptDisplayName(const ScriptTypeInfo* scriptType);

	// "editor.component.<Name>" 키로 로컬라이즈된 컴포넌트 라벨.
	std::string LocalizedComponentLabel(const ComponentTypeInfo& componentType);

	// "editor.property.<Name>" 키로 로컬라이즈된 프로퍼티 라벨.
	std::string LocalizedPropertyLabel(const ReflectPropertyInfo& property);

	// "editor.category.<category>" 키로 로컬라이즈된 카테고리 라벨.
	std::string LocalizedCategoryLabel(const char* category);
}

#endif
