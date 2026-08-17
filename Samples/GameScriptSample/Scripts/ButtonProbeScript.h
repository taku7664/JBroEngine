#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

#include <vector>

// ── CButtonProbeScript ────────────────────────────────────────────────────────
// Button2D 자가검증 스크립트. 자산이 필요 없다 — 레이어·오브젝트를 런타임에 만들고 끝나면 지운다.
//
// ── 포인터를 어떻게 만드는가 ─────────────────────────────────────────────────
// 윈도우에서 마우스는 매 프레임 GetCursorPos 로 덮어써진다 — 합성이 통하지 않는다.
// 그래서 **터치를 주입**한다(Script.Input->InjectTouch). 버튼 시스템은 터치를 마우스보다
// 먼저 보므로 이 경로가 곧 실제 포인터 경로이고, 덤으로 이 프로브가 터치 항목을 겸한다.
//
// ── 왜 프레임을 넘기는가 ─────────────────────────────────────────────────────
// 버튼 상태기는 프레임 경계로 돈다(눌림 = "이번 프레임 down, 지난 프레임 up").
// 한 프레임에 몰면 상태 전이를 볼 수 없다. 한 스텝마다 한 프레임씩 넘긴다.
//
// ── 훅은 어떻게 보는가 ───────────────────────────────────────────────────────
// 훅은 **버튼과 같은 오브젝트**의 스크립트로 간다. 런타임에 스크립트를 붙이는 공개 API 는
// 없으므로(AddScript 는 캔버스의 private), 마지막 절에서 **프로브 자신의 오브젝트에**
// Button2D 를 붙여 프로브가 스스로 훅을 받는다. 카운터 스크립트가 따로 필요 없고,
// 캔버스 저작에 의존하지도 않는다.
//
// ── 여기서 못 보는 것 ────────────────────────────────────────────────────────
// · 시뮬레이션 정지 시 normal 복원 — 프로브는 재생 중에만 돈다.
JBRO_SCRIPT CButtonProbeScript final : public CGameScript
{
	SCRIPT_CLASS(CButtonProbeScript)

protected:
	void OnStart() override;

	// 마지막 절에서 프로브 오브젝트 자신이 버튼이 된다 — 그때 이 훅들이 온다.
	void OnButtonEnter() override { ++m_enterCount; }
	void OnButtonExit()  override { ++m_exitCount; }
	void OnButtonDown()  override { ++m_downCount; }
	void OnButtonUp()    override { ++m_upCount; }
	void OnButtonClick() override { ++m_clickCount; }

private:
	Coroutine RunProbe();

	void Check(bool condition, const char* label);
	void TearDown();

	bool m_started = false;
	int  m_passCount = 0;
	int  m_failCount = 0;

	int  m_enterCount = 0;
	int  m_exitCount  = 0;
	int  m_downCount  = 0;
	int  m_upCount    = 0;
	int  m_clickCount = 0;

	std::vector<SafePtr<CGameObject>> m_spawned;
	SafePtr<CGameLayer>               m_screenLayer;
};
