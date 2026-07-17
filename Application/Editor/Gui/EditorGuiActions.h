#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Utillity/Math/Vector2T.h" // Vector2 (DrawAddObjectMenu 의 spawn 위치)

#include <string>

class CGameCanvas;
class CGameObject;
class CGameLayer;
namespace EditorGuiActions
{
	// ── 컴포넌트 ──────────────────────────────────────────────────────────

	// 팝업 메뉴 항목으로 컴포넌트 카테고리 서브메뉴를 표시.
	// ImGui::BeginPopupContextItem/ContextWindow 안에서 호출할 것.
	bool DrawAddComponentMenu(CGameCanvas& scene, CGameObject* object);

	// 버튼 + 필터 가능한 팝업 조합으로 컴포넌트 추가 UI 표시.
	bool DrawAddComponentButton(CGameCanvas& scene, CGameObject* object, const char* buttonLabel = nullptr);

	// ── 오브젝트 ──────────────────────────────────────────────────────────

	// "Add Object" / "Add Child Object" MenuItem 하나를 표시.
	//   parent == nullptr : 루트에 오브젝트 추가 → 레이블 "Add Object"
	//   parent != nullptr : parent의 자식으로 추가 → 레이블 "Add Child Object"
	// 성공 시 생성된 오브젝트를 선택 상태로 만들고 true 반환.
	//   spawnWorldPos != nullptr : 그 월드 좌표에 생성(씬뷰 우클릭 위치). null = 기본(원점).
	//   layer != nullptr : 그 레이어에 생성(하이어라키의 레이어 컨텍스트 메뉴). null = 기본 레이어.
	bool DrawAddObjectMenu(CGameCanvas& scene, CGameObject* parent, const Vector2* spawnWorldPos = nullptr,
	                       CGameLayer* layer = nullptr);

	bool DrawRemoveObjectMenu(CGameCanvas& scene, CGameObject* object);

	// ── 복사 / 붙여넣기 (직렬화 기반, 클립보드 경유) ───────────────────────────
	// 오브젝트/컴포넌트를 직렬화 문자열로 클립보드에 복사하고, 붙여넣기는 클립보드
	// 텍스트를 역직렬화한다. Paste 메뉴는 클립보드 내용이 맞는 종류일 때만 표시한다.

	// 오브젝트를 클립보드로 복사하는 MenuItem.
	bool DrawCopyObjectMenuItem(const CGameObject& object);
	// 클립보드의 오브젝트를 scene 에 붙여넣는 MenuItem(클립보드가 오브젝트일 때만 표시).
	//   layer == nullptr 이면 현재 선택으로 대상 레이어를 정한다(ResolveTargetLayer).
	bool DrawPasteObjectMenuItem(CGameCanvas& scene, CGameObject* parent = nullptr,
	                             CGameLayer* layer = nullptr);

	// ── 대상 레이어 결정 ──────────────────────────────────────────────────────
	// "새로 놓는 오브젝트가 어느 레이어로 가는가"의 단일 규칙: 레이어를 골랐으면 그 레이어,
	// 아니면 고른 오브젝트가 속한 레이어, 둘 다 없으면 nullptr(= 씬 기본 레이어).
	// 붙여넣기 메뉴와 단축키가 공유한다 — 규칙이 갈리면 같은 동작이 진입점마다 달라진다.
	CGameLayer* ResolveTargetLayer(CGameCanvas& scene);

	// ── 단축키용(메뉴 아님) 복사/붙여넣기 ─────────────────────────────────────
	// 선택된 최상위 오브젝트들을 클립보드로 복사(다중). 선택이 없으면 false.
	bool CopySelectedObjectsToClipboard();
	// 클립보드 오브젝트(들)을 붙여넣고(undo 가능) 붙여넣은 루트를 선택한다.
	//   spawnWorldPos != nullptr : 그룹 중심을 그 월드 좌표로 이동. null = 원본 위치 유지.
	//   layer == nullptr 이면 ResolveTargetLayer 로 현재 선택에서 정한다.
	bool PasteObjectsFromClipboard(CGameCanvas& scene, const Vector2* spawnWorldPos = nullptr,
	                               CGameObject* parent = nullptr, CGameLayer* layer = nullptr);
	// 선택된 최상위 오브젝트들을 한 번의 undo 단위로 삭제한다.
	bool DeleteSelectedObjects(CGameCanvas& scene);
	// 선택된 레이어를 삭제한다(마지막 레이어면 거부). 성공 시 레이어 선택 해제.
	bool DeleteSelectedLayer(CGameCanvas& scene);
	bool HasObjectClipboardData();

	// 컴포넌트를 클립보드로 복사하는 MenuItem.
	bool DrawCopyComponentMenuItem(const class CComponent& component);
	// 클립보드의 컴포넌트를 object 에 붙여넣는 MenuItem(클립보드가 컴포넌트일 때만 표시).
	bool DrawPasteComponentMenuItem(CGameObject& object);

}

#endif
