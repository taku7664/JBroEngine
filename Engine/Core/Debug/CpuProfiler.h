#pragma once

#include <cstddef>
#include <vector>

// 스크립트 인스턴스 하나의 프레임 CPU 시간(Update). 키 = 그 프레임 안에서만 유효한 오브젝트 주소
// (CGameObject*) — 창이 같은 프레임에 이름으로 역해석한다(GPU 드로우순서와 동일 수명 계약).
struct CpuScriptTiming
{
	const void* Object = nullptr;
	double      Microseconds = 0.0;
};

// CPU 프로파일러 보조 서비스 (에디터 진단).
//
// **컴포넌트 풀 당 순회시간**은 CFrameSectionProfiler(canvas m_frameProfiler / Engine.FrameProfiler)가
// 이미 시스템별로 잰다 — CPU 프로파일러 창이 그 값을 읽는다. 이 서비스는 "선택 시에만" 켜는 무거운
// 계측 하나만 맡는다: **스크립트 인스턴스별 Update 시간**. 스크립트 순회만 오브젝트 의존적(유저 코드)
// 이라 오브젝트별 분해가 의미 있고, 나머지 엔진 컴포넌트는 배치 처리라 풀 단위로만 잰다.
//
// 계측 자체가 에디터 전용(JBRO_EDITOR)이라 게임 빌드에선 켜질 일이 없다. 버퍼는 프레임 간
// 재사용한다(논리 개수만 리셋 → 캡처 핫패스에 힙 할당 없음).
class CCpuProfiler
{
public:
	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	// 창이 Script 풀을 선택한 프레임에만 켠다(opt-in — EndFrame 에서 내려간다). ShouldCaptureScripts 로 소비.
	void SetCaptureScripts(bool capture) { m_captureScripts = capture; }
	// ScriptSystem 이 각 script.Update() 앞에서 확인 — enabled + 이번 프레임 캡처 요청일 때만 잰다.
	bool ShouldCaptureScripts() const { return m_enabled && m_captureScripts; }

	// 캡처 직전(프레임 업데이트 시작): 스크립트 타이밍 버퍼의 논리 개수만 리셋한다.
	void BeginFrame() { m_scriptTimingCount = 0; }
	// 캡처 소비 후(업데이트 끝, 창 재요청 전): opt-in 플래그를 내려 창이 매 프레임 다시 요청하게 한다.
	void EndFrame() { m_captureScripts = false; }

	void RecordScriptTiming(const void* object, double microseconds)
	{
		if (m_scriptTimingCount >= m_scriptTimings.size())
		{
			m_scriptTimings.emplace_back();
		}
		m_scriptTimings[m_scriptTimingCount] = CpuScriptTiming{ object, microseconds };
		++m_scriptTimingCount;
	}

	// 이번 프레임 캡처된 스크립트 타이밍(유효 개수 = GetScriptTimingCount, 버퍼는 그보다 클 수 있다).
	const std::vector<CpuScriptTiming>& GetScriptTimings() const { return m_scriptTimings; }
	std::size_t GetScriptTimingCount() const { return m_scriptTimingCount; }

private:
	bool m_enabled = false;
	bool m_captureScripts = false;
	std::vector<CpuScriptTiming> m_scriptTimings;
	std::size_t m_scriptTimingCount = 0;
};
