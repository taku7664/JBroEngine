#pragma once

#include "Utillity/Math/Vector2T.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameCanvas;
class CGameObject;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CButton2DSystem ─ 포인터를 버튼 렉트에 맞춰 보고 상태 훅을 쏜다.
//
//  **CGameSystem 이 아니다.** Canvas::Update 계약이 [스크립트 → flush → 시스템] 이라
//  m_systems 에 넣으면 hover/click 이 항상 한 프레임 낡는다. 버튼은 스크립트의 *입력*이므로
//  물리처럼 전용 단계로 스크립트 앞에 둔다.
//
//  포인터는 **하나**다 — 마우스와 첫 활성 터치를 하나로 정규화한다.
//  손가락 여러 개로 버튼 여럿 동시 누르기는 범위 밖.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class CButton2DSystem
{
public:
	// 스크립트 Update 직전에 1회.
	void Update(CGameCanvas& canvas);

	bool IsPointerOverButton() const { return m_pointerOverButton; }

	// 시뮬레이션 정지 — 눌린 채로 멈춘 상태가 편집 모드에 남지 않게 전부 되돌린다.
	void ResetState(CGameCanvas& canvas);

private:
	struct Pointer
	{
		float X = 0.0f;
		float Y = 0.0f;
		bool  Down = false;
		bool  Valid = false;
	};

	static Pointer ReadPointer();

	// 그리는 순서의 역순으로 훑어 첫 히트에서 멈춘다(위에 있는 버튼이 먹는다).
	// 못 찾으면 nullptr.
	static CGameObject* FindHitButton(CGameCanvas& canvas, const Pointer& pointer);

	static void DispatchEnter(CGameCanvas& canvas, CGameObject& object);
	static void DispatchExit(CGameCanvas& canvas, CGameObject& object);
	static void DispatchDown(CGameCanvas& canvas, CGameObject& object);
	static void DispatchUp(CGameCanvas& canvas, CGameObject& object);
	static void DispatchClick(CGameCanvas& canvas, CGameObject& object);

	bool                 m_pointerOverButton = false;
	bool                 m_pointerWasDown    = false;
	SafePtr<CGameObject> m_hoveredObject;
	// Down 이 시작된 버튼. Up 이 **같은** 버튼에서 끝나야 Click 이다.
	SafePtr<CGameObject> m_pressedObject;
};
