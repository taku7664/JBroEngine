#include "pch.h"
#include "CoroutineTestScript.h"

#include <string>

namespace
{
	// 헬퍼 이름은 TestLog — 엔진의 Core/Logging 에 class Log 가 있어 Log 는 이름 충돌한다.
	void TestLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[Coroutine Test] " + message);
		}
	}

	float ElapsedSeconds()
	{
		return Script.Time ? Script.Time->GetElapsedSeconds() : 0.0f;
	}
	float UnscaledElapsedSeconds()
	{
		return Script.Time ? Script.Time->GetUnscaledElapsedSeconds() : 0.0f;
	}
	std::uint64_t FrameCount()
	{
		return Script.Time ? Script.Time->GetFrameCount() : 0;
	}

	std::string Fixed2(float value)
	{
		// 소수 둘째 자리 반올림 문자열(로그 가독용).
		const int scaled = static_cast<int>(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
		std::string whole = std::to_string(scaled / 100);
		int frac = scaled % 100;
		if (frac < 0)
		{
			frac = -frac;
		}
		std::string fracStr = std::to_string(frac);
		if (fracStr.size() < 2)
		{
			fracStr = "0" + fracStr;
		}
		return whole + "." + fracStr;
	}
}

bool CCoroutineTestScript::UntilReady()
{
	return s_untilReady;
}

void CCoroutineTestScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunTests());
}

void CCoroutineTestScript::OnUpdate()
{
	// Wait::Until 검증용 — 몇 프레임 뒤 정적 플래그를 켠다.
	if (m_untilCountdown > 0)
	{
		--m_untilCountdown;
		if (0 == m_untilCountdown)
		{
			s_untilReady = true;
		}
	}
}

Coroutine CCoroutineTestScript::RunTests()
{
	TestLog("START");

	// 1) Wait::Seconds — 스케일 시간(≈1.0s).
	{
		const float start = ElapsedSeconds();
		TestLog("Seconds: waiting 1.0s ...");
		co_await Wait::Seconds(1.0f);
		TestLog("Seconds: resumed after " + Fixed2(ElapsedSeconds() - start) + "s (expect ~1.00)");
	}

	// 2) Wait::Frames — 정확히 5 프레임.
	{
		const std::uint64_t start = FrameCount();
		TestLog("Frames: waiting 5 frames ...");
		co_await Wait::Frames(5);
		TestLog("Frames: resumed after " + std::to_string(FrameCount() - start) + " frames (expect 5)");
	}

	// 3) Wait::SecondsRealtime — 언스케일 시간(≈0.5s). timeScale 을 바꿔도 이건 실시간.
	{
		const float start = UnscaledElapsedSeconds();
		TestLog("SecondsRealtime: waiting 0.5s (unscaled) ...");
		co_await Wait::SecondsRealtime(0.5f);
		TestLog("SecondsRealtime: resumed after " + Fixed2(UnscaledElapsedSeconds() - start) + "s (expect ~0.50)");
	}

	// 4) 중첩 코루틴 — 자식이 끝날 때까지 대기 후 이어서 재개.
	{
		TestLog("Nested: entering child ...");
		co_await ChildRoutine();
		TestLog("Nested: child returned, parent resumed (PASS)");
	}

	// 5) Wait::Until — 정적 술어가 true 될 때까지(약 10 프레임 뒤 OnUpdate 가 켬).
	{
		s_untilReady = false;
		m_untilCountdown = 10;
		const std::uint64_t start = FrameCount();
		TestLog("Until: waiting for predicate (flag set in ~10 frames) ...");
		co_await Wait::Until(&CCoroutineTestScript::UntilReady);
		TestLog("Until: predicate satisfied after " + std::to_string(FrameCount() - start) + " frames (PASS)");
	}

	// 5-b) Wait::Until — **캡처 람다**. 술어가 지역 상태(목표 프레임)와 this 를 함께 캡처한다.
	// 술어는 코루틴 프레임 안에 값으로 복사되므로 힙 할당 없이 대기 내내 살아 있다.
	{
		const std::uint64_t start = FrameCount();
		const std::uint64_t target = start + 5;
		int callCount = 0;
		TestLog("Until(lambda): waiting 5 frames via capturing predicate ...");
		co_await Wait::Until([this, target, &callCount]()
		{
			++callCount;
			return FrameCount() >= target;
		});
		const std::uint64_t elapsed = FrameCount() - start;
		TestLog("Until(lambda): resumed after " + std::to_string(elapsed) + " frames, predicate called "
			+ std::to_string(callCount) + " times (expected 5 frames) "
			+ ((5 == elapsed) ? "(PASS)" : "(FAIL)"));
	}

	// 6) Wait::FixedUpdate — 다음 고정 스텝에 재개.
	{
		TestLog("FixedUpdate: waiting for next fixed step ...");
		co_await Wait::FixedUpdate();
		TestLog("FixedUpdate: resumed on fixed step (PASS)");
	}

	// 7) StopCoroutine — 긴 코루틴을 시작하고 3 프레임 뒤 중지. 중지되면 완료 로그가 안 나와야 한다.
	{
		m_stopId = StartCoroutine(LongRoutine());
		TestLog("Stop: started LongRoutine; will stop after 3 frames ...");
		co_await Wait::Frames(3);
		StopCoroutine(m_stopId);
		TestLog("Stop: called StopCoroutine. You should NOT see 'LongRoutine finished' below.");
		co_await Wait::Seconds(1.0f);   // 중지가 실패했다면 이 사이에 완료 로그가 떴을 것.
		TestLog("Stop: 1s elapsed with no completion log => Stop PASS");
	}

	TestLog("ALL DONE");
}

Coroutine CCoroutineTestScript::ChildRoutine()
{
	TestLog("Nested child: started, waiting 3 frames ...");
	co_await Wait::Frames(3);
	TestLog("Nested child: finished");
}

Coroutine CCoroutineTestScript::LongRoutine()
{
	// 100 프레임 뒤 완료 — 중지 검증에서 이 로그가 뜨면 FAIL(중지 안 됨).
	co_await Wait::Frames(100);
	TestLog("Stop: LongRoutine finished (FAIL if this appears after StopCoroutine)");
}
