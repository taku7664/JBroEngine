#pragma once

#include <cstdint>
#include <vector>

// GPU 타임스탬프 기반 구간 타이머(진단용).
//
// GPU 타임스탬프는 비동기다 — 커맨드 스트림에 쿼리를 꽂아 두면 GPU 가 나중에 실행하고,
// 결과는 몇 프레임 지연되어 읽힌다. 지금 읽으려 하면 GPU-CPU 동기화 스톨이 걸려 측정 자체가
// 프레임을 망치므로, 구현체는 프레임인플라이트 링버퍼를 두고 N프레임 전 결과를 돌려준다.
//
// 백엔드마다 지원 여부가 다르다(일부 웹 환경은 미지원). GetGpuTimer 가 null 이거나
// IsSupported()==false 면 UI 는 안내만 띄운다.

// 한 구간(레이어/패스 등)의 측정 결과.
struct GpuTimerResult
{
	// 호출자가 준 불투명 키(예: CGameLayer*). RHI 는 내용을 모르고, 결과를 다시 이 키로 돌려준다.
	const void* Key = nullptr;
	double      Milliseconds = 0.0;
};

// 잘못된/열리지 않은 구간 식별자. BeginScope 실패 시 반환되고, EndScope 는 이를 무시한다.
inline constexpr std::uint32_t INVALID_GPU_SCOPE = 0xFFFFFFFFu;

class IRHIGpuTimer
{
public:
	virtual ~IRHIGpuTimer() = default;

	// 이 백엔드/디바이스가 GPU 타임스탬프 쿼리를 지원하는가.
	virtual bool IsSupported() const = 0;

	// 계측 on/off. 꺼지면 BeginFrame/BeginScope 등이 사실상 no-op 이 되어 비용이 0 에 수렴한다.
	virtual void SetEnabled(bool enabled) = 0;

	// 프레임 경계. RHI 디바이스의 BeginFrame/EndFrame 에서 호출한다.
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;

	// 구간. BeginScope 가 반환한 id 를 EndScope 에 넘긴다. 같은 프레임 안에서만 유효하다.
	// 지원 안 되거나 프레임이 열려있지 않으면 INVALID_GPU_SCOPE 를 반환한다.
	virtual std::uint32_t BeginScope(const void* key) = 0;
	virtual void EndScope(std::uint32_t scopeId) = 0;

	// 가장 최근에 해소된(N프레임 전) 프레임의 구간별 결과. BeginFrame 에서 갱신된다.
	virtual const std::vector<GpuTimerResult>& GetResults() const = 0;
};
