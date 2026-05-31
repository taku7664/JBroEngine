#pragma once

#include "ThirdParty/imgui/imgui.h"    // ImU32, ImVec2

#include <cstddef>
#include <cstdint>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ImAudioVisualizer
//
//  오디오 파형 시각화를 위한 ImItem.  PCM 데이터를 한 번 분석해 peak-summary
//  를 캐시한 뒤 매 프레임 ImDrawList 로 막대 형태로 그린다 (상하 대칭, 중앙
//  기준).  PlayheadFraction 을 지정하면 그 비율까지를 PlayedColor 로 칠해
//  진행도를 표시한다.
//
//  현재는 정적 PCM(파일 전체) 분석 모드만 제공.  스트리밍/실시간 입력은 추후
//  AppendSamples 류 API 로 확장 가능 — 내부 m_peaks 만 갱신하면 그리기 로직은
//  변경 없음.
//
//  사용 예:
//      ImAudioVisualizer vis;
//      vis.SetPcmData(pcm.data(), pcm.size(), 48000, 2, EAudioFormat::PCM_F32)
//         .SetBarCount(128)
//         .SetPlayedColor(IM_COL32(255, 255, 255, 255))
//         .SetColor      (IM_COL32(120, 120, 120, 255));
//      ...
//      vis.SetPlayheadFraction(pos / total);
//      vis(ImVec2(-FLT_MIN, 60.0f));
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class ImAudioVisualizer
{
public:
	// PCM 샘플 포맷 — 자체 정의 (외부 오디오 모듈에 의존하지 않음).
	// 다른 모듈의 enum 과는 cast 로 손쉽게 변환 가능.
	enum class ESampleFormat : std::uint8_t
	{
		S16,   // 16-bit signed PCM (interleaved)
		F32,   // 32-bit float       (interleaved, miniaudio 등의 기본값)
	};

public:
	ImAudioVisualizer();
	~ImAudioVisualizer();

	// ── PCM 분석 ────────────────────────────────────────────────────────
	// data            : interleaved PCM 시작 포인터
	// byteCount       : data 의 전체 바이트 수
	// sampleRate      : Hz (시각화 자체엔 직접 영향이 없지만 보관)
	// channels        : 1 / 2 / ... (interleaved 가정. 다채널은 채널 평균.)
	// fmt             : S16 또는 F32
	void SetPcmData(const void* data, std::size_t byteCount,
	                std::uint32_t sampleRate, std::uint16_t channels,
	                ESampleFormat fmt);
	void Clear();

	// 데이터가 등록되어 있는지.
	bool HasData() const { return false == m_peaks.empty(); }

	// ── 시각 옵션 (chain 가능) ──────────────────────────────────────────
	ImAudioVisualizer& SetBarCount       (int   count);        // 막대 개수 (기본 128)
	ImAudioVisualizer& SetBarThickness   (float thicknessPx);  // 각 막대 가로 두께
	ImAudioVisualizer& SetBarGap         (float gapPx);        // 막대 간 간격
	ImAudioVisualizer& SetColor          (ImU32 color);        // 아직 재생되지 않은 부분
	ImAudioVisualizer& SetPlayedColor    (ImU32 color);        // 재생된 부분 (밝게)
	ImAudioVisualizer& SetBackgroundColor(ImU32 color);        // 배경 (0 이면 그리지 않음)
	ImAudioVisualizer& SetAmplitudeGain  (float gain);         // 진폭 가시성 보정 (기본 1.0)

	// 재생 진행 비율 [0,1]. 이 비율의 좌측까지가 PlayedColor 로 칠해진다.
	ImAudioVisualizer& SetPlayheadFraction(float frac01);

	// ── 그리기 — 호출은 매 프레임 ────────────────────────────────────────
	// size.x < 0 이면 가용 폭을 채운다(-FLT_MIN 권장). size.y 는 막대 높이.
	void operator()(const ImVec2& size);
	void Draw      (const ImVec2& size) { (*this)(size); }

private:
	// 입력 PCM 을 m_bars 개의 amplitude 버킷으로 다운샘플.  각 버킷은 그 구간의
	// 채널 평균 abs 신호의 최대치 (peak).  결과는 [0,1] 로 정규화.
	void BuildPeaks(const void* data, std::size_t byteCount,
	                ESampleFormat fmt, std::uint16_t channels);

private:
	std::vector<float> m_peaks;                                // [0,1] amplitude per bucket
	std::uint32_t      m_sampleRate      = 0;
	std::uint16_t      m_channels        = 0;
	ESampleFormat      m_format          = ESampleFormat::F32;

	int                m_bars            = 128;
	float              m_barThickness    = 2.0f;
	float              m_barGap          = 1.0f;
	float              m_amplitudeGain   = 1.0f;
	ImU32              m_color           = IM_COL32(120, 120, 120, 255);
	ImU32              m_playedColor     = IM_COL32(255, 255, 255, 255);
	ImU32              m_backgroundColor = 0;
	float              m_playheadFrac    = 0.0f;
};
