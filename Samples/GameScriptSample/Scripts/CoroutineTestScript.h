#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CCoroutineTestScript ──────────────────────────────────────────────────────
// 코루틴 시스템 자가검증 스크립트. 오브젝트에 붙이고 재생하면 로그로 각 기능의 결과를
// 순서대로 출력한다(Debug Log 창에서 확인). 기대값과 실제값을 함께 찍어 눈으로 PASS/FAIL 판정.
//
// 검증 항목:
//   · Wait::Seconds          — 스케일 시간 경과(≈1.0s)
//   · Wait::Frames           — N 프레임 경과(정확히 요청 프레임 수)
//   · Wait::SecondsRealtime  — 언스케일 시간 경과(≈0.5s)
//   · 중첩 코루틴(co_await)   — 자식 코루틴 완료까지 대기 후 이어서 재개
//   · Wait::Until            — 술어가 true 될 때까지 대기(함수 포인터 / 캡처 람다 둘 다)
//   · Tween::Value / MoveTo  — 코루틴 위의 트윈(끝값 정확도 포함)
//   · Wait::FixedUpdate      — 다음 고정 스텝에 재개
//   · StopCoroutine          — 중지한 코루틴은 완료 로그를 남기지 않음(중지 검증)
class CCoroutineTestScript final : public CGameScript
{
	SCRIPT_CLASS(CCoroutineTestScript)

protected:
	void OnStart()  override;
	void OnUpdate() override;

private:
	// 순차 검증 마스터 코루틴 + 하위 코루틴들(본체는 .cpp).
	Coroutine RunTests();
	Coroutine ChildRoutine();
	Coroutine LongRoutine();

	// 5) 는 함수 포인터 경로를 확인하려고 정적 술어를 쓴다. 캡처 람다도 받으므로(5-b 참조)
	// 실제 게임 코드는 보통 람다 쪽이 더 간단하다 — 상태를 정적으로 끌어올릴 필요가 없다.
	static bool UntilReady();
	static inline bool s_untilReady = false;

	int         m_untilCountdown = 0;   // OnUpdate 가 0 까지 세면 s_untilReady 를 켠다.
	bool        m_started = false;       // RunTests 1회 시작 가드.
	CoroutineId m_stopId;                // 중지 검증 대상 코루틴.
};
