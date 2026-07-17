#pragma once

class CGameCanvas;
class CGameObject;
class IDebugDraw2D;

namespace CanvasDebugDraw
{
	// Submit debug primitives for all debug-visible components in the canvas.
	//
	// selectedObject    — 단일 선택 오브젝트. nullptr 이면 기본 색상 사용.
	// resW / resH       — 게임 렌더 타겟 해상도. 0 이하이면 카메라 프러스텀 미표시.
	// activeCompType    — Inspector 우측 패널에 현재 표시 중인 컴포넌트 타입 이름.
	//                     nullptr 이면 미선택 상태.
	//                     "CircleCollider2D" / "PolygonCollider2D" 일 때:
	//                       - selectedEntity 콜라이더를 푸른색 + 2px 두께로 강조.
	//                     그 외에는 콜라이더 색상을 변경하지 않음
	//                     (다중 선택 시에도 색 변경 없음).
	//
	// Call once per frame after canvas update, before the editor renders the overlay.
	void Submit(
		const CGameCanvas&       canvas,
		IDebugDraw2D&       debugDraw,
		const CGameObject*  selectedObject  = nullptr,
		float               resW            = 0.0f,
		float               resH            = 0.0f,
		const char*         activeCompType  = nullptr);
}
