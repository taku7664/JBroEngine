#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CSpriteBoundsProbeScript ─────────────────────────────────────────────────
// CRenderer2DComponent 경계 + 스프라이트 피벗 자가검증 스크립트.
//
// ── 무엇을 어떻게 보는가 ─────────────────────────────────────────────────────
// 경계는 화면으로 확인할 수 없으므로(에디터는 스크립트가 못 연다) 숫자로 본다.
//
// 기대값은 **구현을 불러서 만들지 않는다.** 프레임 픽셀 크기와 유효 PPU 는 사실로 읽고,
// 거기서 경계를 손으로 쓴 식으로 다시 세운 뒤 GetLocalBounds() 와 대조한다.
//
// 첫 프레임에는 렌더 시스템이 아직 자산을 해석하지 않아 경계가 비어 있다 —
// 그 계약 자체가 검사 대상이라 co_await 전에 한 번 읽는다.
//
// 피벗이 실제로 먹는지는 자산의 임포트 옵션에 달려 있어 런타임에 바꿀 수 없다.
// 그래서 피벗 값과 계산된 중심을 **로그로 남긴다** — .jmeta 의 PivotY 를 바꿔 다시 돌리면
// 중심이 같이 움직이는지 눈으로 대조할 수 있다.
JBRO_SCRIPT CSpriteBoundsProbeScript final : public CGameScript
{
	SCRIPT_CLASS(CSpriteBoundsProbeScript)

protected:
	void OnStart() override;

private:
	Coroutine RunProbe();

	void Check(bool condition, const char* label);
	void CheckNear(float actual, float expected, float tolerance, const char* label);
	void CheckRect(const Rect& actual, const Rect& expected, float tolerance, const char* label);

	void TearDown();

	int m_passCount = 0;
	int m_failCount = 0;

	std::vector<SafePtr<CGameObject>> m_spawned;
	bool m_started = false;
};
