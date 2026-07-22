#pragma once

#include <chrono>
#include <vector>

// 프레임을 구간별로 쪼개 재는 수집기.
//
// 슬롯은 **라벨 포인터**로 식별한다. typeid(x).name() 이나 문자열 리터럴처럼 정적 저장 기간을
// 가진 것만 넘길 것(문자열 내용 비교를 하지 않는다 — 프레임 경로에서 strcmp 를 돌릴 이유가 없다).
// 리터럴을 쓸 때는 같은 번역 단위 안에서만 쓸 것. TU 가 다르면 같은 내용이라도 주소가 다를 수 있다.
//
// 왜 누적하고 나중에 접는가:
//   FixedUpdate 는 한 프레임에 0~8 번 돈다. 호출마다 평균에 접으면 마지막 스텝 값만 남아
//   물리 비용이 최대 8 배 과소보고된다. 프레임 안에서는 더하기만 하고, EndFrame 에서 한 번 접는다.
//
// 안 불린 구간은 평균이 0 으로 수렴한다. 멈춘 시스템이 옛 값을 계속 보여 주는 것보다 낫다.
struct FrameSectionTiming
{
	const char* Label = nullptr;
	bool         IsFixedStep = false;
	double       AverageMicroseconds = 0.0;
	unsigned int CallsPerFrame = 0;          // 마지막으로 접힌 프레임의 호출 횟수
	double       AccumulatedMicroseconds = 0.0;
	unsigned int CallsThisFrame = 0;
};

class CFrameSectionProfiler
{
public:
	void Accumulate(const char* label, bool isFixedStep, double microseconds)
	{
		FrameSectionTiming& section = Acquire(label, isFixedStep);
		section.AccumulatedMicroseconds += microseconds;
		++section.CallsThisFrame;
	}

	// 프레임당 정확히 한 번. 프레임의 마지막 구간이 끝난 뒤에 부를 것.
	void EndFrame()
	{
		for (FrameSectionTiming& section : m_sections)
		{
			// 지수 평활. 순간값은 프레임마다 몇 배씩 흔들려 눈으로 읽을 수가 없다.
			// 0.05 는 대략 최근 20 프레임을 보는 창.
			const double delta = section.AccumulatedMicroseconds - section.AverageMicroseconds;
			section.AverageMicroseconds += delta * SmoothingFactor;
			section.CallsPerFrame = section.CallsThisFrame;
			section.AccumulatedMicroseconds = 0.0;
			section.CallsThisFrame = 0;
		}
	}

	const std::vector<FrameSectionTiming>& GetSections() const { return m_sections; }

	void Reset() { m_sections.clear(); }

private:
	static constexpr double SmoothingFactor = 0.05;

	// 구간이 십여 개라 해시맵보다 선형 탐색이 빠르고 단순하다.
	FrameSectionTiming& Acquire(const char* label, bool isFixedStep)
	{
		for (FrameSectionTiming& section : m_sections)
		{
			if (section.Label == label && section.IsFixedStep == isFixedStep)
			{
				return section;
			}
		}

		FrameSectionTiming created;
		created.Label = label;
		created.IsFixedStep = isFixedStep;
		m_sections.push_back(created);
		return m_sections.back();
	}

	std::vector<FrameSectionTiming> m_sections;
};

// 구간 하나를 재는 RAII.
//
// 게임 빌드에서는 몸통이 전부 비어 인라인 후 사라진다. 호출부에 #if 를 뿌리지 않으려고
// 매크로 대신 이 방식을 쓴다(매크로는 숨은 변수 이름을 만들어 낸다).
class CFrameSectionScope
{
public:
#if defined(JBRO_EDITOR)
	CFrameSectionScope(CFrameSectionProfiler& profiler, const char* label, bool isFixedStep)
		: m_profiler(&profiler)
		, m_label(label)
		, m_isFixedStep(isFixedStep)
		, m_begin(std::chrono::steady_clock::now())
	{
	}

	~CFrameSectionScope()
	{
		const double microseconds = std::chrono::duration<double, std::micro>(
			std::chrono::steady_clock::now() - m_begin).count();
		m_profiler->Accumulate(m_label, m_isFixedStep, microseconds);
	}
#else
	CFrameSectionScope(CFrameSectionProfiler&, const char*, bool) {}
#endif

	CFrameSectionScope(const CFrameSectionScope&) = delete;
	CFrameSectionScope& operator=(const CFrameSectionScope&) = delete;

#if defined(JBRO_EDITOR)
private:
	CFrameSectionProfiler* m_profiler = nullptr;
	const char* m_label = nullptr;
	bool m_isFixedStep = false;
	std::chrono::steady_clock::time_point m_begin;
#endif
};
