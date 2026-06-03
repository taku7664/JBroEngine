#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>

class CScene;
class CGameObject;
struct ComponentTypeInfo;
struct ReflectPropertyInfo;
struct ScriptTypeInfo;

namespace EditorGuiDrawHelpers
{
	// ── 컴포넌트 ──────────────────────────────────────────────────────────

	// 팝업 메뉴 항목으로 컴포넌트 카테고리 서브메뉴를 표시.
	// ImGui::BeginPopupContextItem/ContextWindow 안에서 호출할 것.
	bool DrawAddComponentMenu(CScene& scene, CGameObject* object);

	// 버튼 + 팝업 조합으로 컴포넌트 추가 UI 표시.
	bool DrawAddComponentButton(CScene& scene, CGameObject* object);

	// ── 오브젝트 ──────────────────────────────────────────────────────────

	// "Add Object" / "Add Child Object" MenuItem 하나를 표시.
	//   parent == nullptr : 루트에 오브젝트 추가 → 레이블 "Add Object"
	//   parent != nullptr : parent의 자식으로 추가 → 레이블 "Add Child Object"
	// 성공 시 생성된 오브젝트를 선택 상태로 만들고 true 반환.
	bool DrawAddObjectMenu(CScene& scene, CGameObject* parent);

	bool DrawRemoveObjectMenu(CScene& scene, CGameObject* object);

	// ── 복사 / 붙여넣기 (직렬화 기반, 클립보드 경유) ───────────────────────────
	// 오브젝트/컴포넌트를 직렬화 문자열로 클립보드에 복사하고, 붙여넣기는 클립보드
	// 텍스트를 역직렬화한다. Paste 메뉴는 클립보드 내용이 맞는 종류일 때만 표시한다.

	// 오브젝트를 클립보드로 복사하는 MenuItem.
	bool DrawCopyObjectMenuItem(const CGameObject& object);
	// 클립보드의 오브젝트를 scene 에 붙여넣는 MenuItem(클립보드가 오브젝트일 때만 표시).
	bool DrawPasteObjectMenuItem(CScene& scene);

	// 컴포넌트를 클립보드로 복사하는 MenuItem.
	bool DrawCopyComponentMenuItem(const class CComponent& component);
	// 클립보드의 컴포넌트를 object 에 붙여넣는 MenuItem(클립보드가 컴포넌트일 때만 표시).
	bool DrawPasteComponentMenuItem(CGameObject& object);

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
