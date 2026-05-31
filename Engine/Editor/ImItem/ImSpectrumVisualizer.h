#pragma once

#include "ThirdParty/imgui/imgui.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ImSpectrumVisualizer ─ 실시간 스펙트럼 (이퀄라이저 스타일) 시각화
//
//  사용 흐름:
//    1) BindPcm(...)  — 분석할 PCM 버퍼 등록 (소유권 X, 호출자가 lifetime 보장)
//    2) Tick(currentSec, dt)  — 매 프레임 호출.  현재 재생 위치를 중심으로 한
//       FFT 윈도우를 굴려 막대 값을 부드럽게 갱신.
//    3) operator()(size)  — 그리기.
//
//  FFT 결과는 log-frequency mapping 으로 N 개의 막대에 매핑되며, 막대마다
//  스무딩(EMA) 과 피크 홀드 + 감쇠가 적용된다.  색은 진폭에 따라 low/mid/high
//  세 색의 그라데이션으로 보간.
//
//  외부 오디오 모듈에 의존하지 않는다 — 자체 ESampleFormat 만 사용.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class ImSpectrumVisualizer
{
public:
	enum class ESampleFormat : std::uint8_t
	{
		S16, F32,
	};

public:
	ImSpectrumVisualizer();
	~ImSpectrumVisualizer();

	// PCM 등록 — 호출자가 data lifetime 을 보장해야 한다.
	void BindPcm(const void* data, std::size_t byteCount,
	             std::uint32_t sampleRate, std::uint16_t channels,
	             ESampleFormat fmt);
	void Unbind();
	bool HasPcm() const { return nullptr != m_pcm; }

	// 매 프레임 — currentSeconds 위치의 윈도우로 FFT, deltaSeconds 로 피크 감쇠.
	void Tick(double currentSeconds, float deltaSeconds);

	// 그리기.  Tick 이 먼저 호출되어야 의미 있는 막대가 보인다.
	void operator()(const ImVec2& size);

	// ── chain setter ───────────────────────────────────────────────────
	ImSpectrumVisualizer& SetBarCount       (int   bars);              // 막대 개수
	ImSpectrumVisualizer& SetBarGap         (float gapPx);             // 간격
	ImSpectrumVisualizer& SetSmoothingFactor(float k);                 // [0,1) — 클수록 천천히 움직임
	ImSpectrumVisualizer& SetPeakDecay      (float perSecond);         // 피크 감쇠 속도
	ImSpectrumVisualizer& SetMinFreq        (float hz);                // 표시 시작 주파수
	ImSpectrumVisualizer& SetMaxFreq        (float hz);                // 표시 끝 주파수
	ImSpectrumVisualizer& SetBackgroundColor(ImU32 color);             // 0 이면 그리지 않음
	ImSpectrumVisualizer& SetColorLow       (ImU32 color);             // 작은 진폭의 색
	ImSpectrumVisualizer& SetColorMid       (ImU32 color);             // 중간
	ImSpectrumVisualizer& SetColorHigh      (ImU32 color);             // 큰 진폭의 색
	ImSpectrumVisualizer& SetShowPeakMarkers(bool  show);
	ImSpectrumVisualizer& SetFftSize        (int   powerOfTwo);        // 256/512/1024/2048 권장
	ImSpectrumVisualizer& SetRounding       (float r);                 // 막대 모서리 둥글기 (0=직각)

private:
	void EnsureSizes();
	void RunFFT(std::size_t centerFrame);
	static void FFT(float* re, float* im, int N);

private:
	// ── bound PCM (소유 X) ─────────────────────────────────────────────
	const std::uint8_t* m_pcm           = nullptr;
	std::size_t         m_pcmByteCount  = 0;
	std::uint32_t       m_sampleRate    = 0;
	std::uint16_t       m_channels      = 0;
	ESampleFormat       m_format        = ESampleFormat::F32;

	// ── FFT scratch ────────────────────────────────────────────────────
	int                 m_fftSize       = 1024;
	std::vector<float>  m_window;        // Hann window
	std::vector<float>  m_real;
	std::vector<float>  m_imag;
	std::vector<float>  m_magnitudes;    // size m_fftSize/2

	// ── 막대 상태 ──────────────────────────────────────────────────────
	int                 m_bars              = 48;
	float               m_barGap            = 2.0f;
	float               m_smoothing         = 0.65f;
	float               m_peakDecay         = 1.5f;
	float               m_minFreq           = 60.0f;
	float               m_maxFreq           = 16000.0f;
	float               m_rounding          = 2.0f;
	ImU32               m_bgColor           = 0;
	ImU32               m_colorLow          = IM_COL32( 80, 130, 240, 255);
	ImU32               m_colorMid          = IM_COL32( 60, 220, 130, 255);
	ImU32               m_colorHigh         = IM_COL32(240,  80, 180, 255);
	bool                m_showPeakMarkers   = true;

	std::vector<float>  m_barValues;   // smoothed [0,1]
	std::vector<float>  m_peakValues;  // peak hold [0,1]
};
