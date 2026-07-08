#include "pch.h"
#include "ImAudioVisualizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

ImAudioVisualizer::ImAudioVisualizer() = default;
ImAudioVisualizer::~ImAudioVisualizer() = default;

void ImAudioVisualizer::SetPcmData(const void* data, std::size_t byteCount,
                                   std::uint32_t sampleRate, std::uint16_t channels,
                                   ImAudioVisualizer::ESampleFormat fmt)
{
	m_sampleRate = sampleRate;
	m_channels   = (channels > 0) ? channels : 1;
	m_format     = fmt;

	if (nullptr == data || 0 == byteCount || 0 == channels)
	{
		m_peaks.clear();
		return;
	}
	BuildPeaks(data, byteCount, fmt, m_channels);
}

void ImAudioVisualizer::Clear()
{
	m_peaks.clear();
	m_playheadFrac = 0.0f;
}

// ── chain setter ───────────────────────────────────────────────────────────
ImAudioVisualizer& ImAudioVisualizer::SetBarCount(int count)
{
	const int clamped = std::max(1, std::min(8192, count));
	if (clamped != m_bars)
	{
		m_bars = clamped;
		// 막대 개수가 바뀌면 peak 버킷도 다시 만들어야 정확하지만, 다음 SetPcmData
		// 때 자동으로 반영되므로 여기서는 기존 m_peaks 를 그대로 두고 그리기 시
		// 보간/스킵으로 대응. (사용자가 분석 후 막대 개수를 또 바꿔도 깨지지 않게.)
	}
	return *this;
}

ImAudioVisualizer& ImAudioVisualizer::SetBarThickness(float thicknessPx)
{
	m_barThickness = std::max(0.5f, thicknessPx);
	return *this;
}

ImAudioVisualizer& ImAudioVisualizer::SetBarGap(float gapPx)
{
	m_barGap = std::max(0.0f, gapPx);
	return *this;
}

ImAudioVisualizer& ImAudioVisualizer::SetColor(ImU32 color)            { m_color = color;           return *this; }
ImAudioVisualizer& ImAudioVisualizer::SetPlayedColor(ImU32 color)      { m_playedColor = color;     return *this; }
ImAudioVisualizer& ImAudioVisualizer::SetBackgroundColor(ImU32 color)  { m_backgroundColor = color; return *this; }

ImAudioVisualizer& ImAudioVisualizer::SetAmplitudeGain(float gain)
{
	m_amplitudeGain = std::max(0.01f, gain);
	return *this;
}

ImAudioVisualizer& ImAudioVisualizer::SetPlayheadFraction(float frac01)
{
	m_playheadFrac = std::max(0.0f, std::min(1.0f, frac01));
	return *this;
}

// ── PCM 다운샘플 → peak summary ─────────────────────────────────────────────
void ImAudioVisualizer::BuildPeaks(const void* data, std::size_t byteCount,
                                   ImAudioVisualizer::ESampleFormat fmt, std::uint16_t channels)
{
	const int bucketCount = m_bars;
	m_peaks.assign(static_cast<std::size_t>(bucketCount), 0.0f);

	// 총 프레임 수 = byteCount / (bytesPerSample * channels)
	const std::size_t bytesPerSample = (ESampleFormat::S16 == fmt) ? 2u : 4u;
	const std::size_t bytesPerFrame  = bytesPerSample * channels;
	if (0 == bytesPerFrame) return;

	const std::size_t totalFrames = byteCount / bytesPerFrame;
	if (0 == totalFrames) return;

	// 각 버킷이 담당할 프레임 범위.  마지막 버킷이 남은 frames 를 다 가져가도록.
	const double framesPerBucket = static_cast<double>(totalFrames) / static_cast<double>(bucketCount);

	const std::uint8_t* base = static_cast<const std::uint8_t*>(data);

	// 버킷별 RMS (root-mean-square) — 구간 평균 에너지.
	// max(abs(sample)) 방식은 버킷당 수만 샘플이 들어가면 거의 항상 1.0 근처가 잡혀
	// 곡 전체가 만땅으로 보인다.  RMS 는 큰 구간/조용한 구간이 분명히 다르게 나옴.
	for (int bucket = 0; bucket < bucketCount; ++bucket)
	{
		const std::size_t frameBegin = static_cast<std::size_t>(bucket * framesPerBucket);
		std::size_t       frameEnd   = static_cast<std::size_t>((bucket + 1) * framesPerBucket);
		if (frameEnd > totalFrames) frameEnd = totalFrames;
		if (frameEnd <= frameBegin)
		{
			m_peaks[bucket] = 0.0f;
			continue;
		}

		double sumSquares = 0.0;
		std::size_t sampleCount = 0;
		for (std::size_t f = frameBegin; f < frameEnd; ++f)
		{
			const std::uint8_t* framePtr = base + f * bytesPerFrame;
			for (std::uint16_t ch = 0; ch < channels; ++ch)
			{
				float sample = 0.0f;
				if (ESampleFormat::S16 == fmt)
				{
					const std::int16_t s = *reinterpret_cast<const std::int16_t*>(framePtr + ch * 2);
					sample = static_cast<float>(s) / 32768.0f;
				}
				else
				{
					sample = *reinterpret_cast<const float*>(framePtr + ch * 4);
				}
				sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
				++sampleCount;
			}
		}

		const double rms = (sampleCount > 0)
			? std::sqrt(sumSquares / static_cast<double>(sampleCount))
			: 0.0;
		m_peaks[bucket] = static_cast<float>(rms);
	}

	// 정규화 — RMS 최댓값을 1.0 으로 맞춘다.  이후 SetAmplitudeGain 으로 추가 조정.
	float maxRms = 0.0f;
	for (float v : m_peaks) if (v > maxRms) maxRms = v;
	if (maxRms > 0.001f)
	{
		const float inv = 1.0f / maxRms;
		for (float& v : m_peaks) v *= inv;
	}
}

// ── 그리기 ──────────────────────────────────────────────────────────────────
void ImAudioVisualizer::operator()(const ImVec2& size)
{
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float  w = (size.x < 0.0f) ? avail.x : size.x;
	const float  h = (size.y <= 0.0f) ? 60.0f  : size.y;
	if (w < 1.0f || h < 1.0f) return;

	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const ImVec2 p1(p0.x + w, p0.y + h);
	ImDrawList*  draw = ImGui::GetWindowDrawList();

	// invisible 위젯을 확보해서 layout 이 이 영역을 차지하도록.
	ImGui::Dummy(ImVec2(w, h));

	if (0 != m_backgroundColor)
	{
		draw->AddRectFilled(p0, p1, m_backgroundColor);
	}

	if (m_peaks.empty())
	{
		// 데이터 없음 — 중앙 가로선만 그려 placeholder 로.
		const float midY = (p0.y + p1.y) * 0.5f;
		draw->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), m_color, 1.0f);
		return;
	}

	const int   peakCount = static_cast<int>(m_peaks.size());
	const int   barCount  = std::min(m_bars, peakCount);
	if (barCount <= 0) return;

	const float stride   = (m_barThickness + m_barGap);
	// 폭 안에 들어갈 수 있는 막대 개수의 상한 — 너무 많이 요청되면 자동으로 줄임.
	const int   maxBars  = std::max(1, static_cast<int>(std::floor(w / std::max(0.5f, stride))));
	const int   useBars  = std::min(barCount, maxBars);

	const float midY     = (p0.y + p1.y) * 0.5f;
	const float halfH    = (h * 0.5f);

	const float playedPx = w * m_playheadFrac;
	const float playedX  = p0.x + playedPx;

	for (int i = 0; i < useBars; ++i)
	{
		// 입력 peak 인덱스 → useBars 에 비례 매핑 (m_bars != useBars 인 경우 대응).
		const int srcIdx = (peakCount == useBars)
			? i
			: static_cast<int>(static_cast<long long>(i) * peakCount / useBars);
		float amp = m_peaks[srcIdx] * m_amplitudeGain;
		amp = std::max(0.0f, std::min(1.0f, amp));

		const float halfBar = amp * halfH;
		// 가운데에서 위·아래로 대칭으로 그리되, 너무 얇으면 최소 1px 보장.
		const float drawHalfBar = std::max(0.5f, halfBar);

		const float barX = p0.x + i * stride;
		const ImVec2 bp0(barX,                          midY - drawHalfBar);
		const ImVec2 bp1(barX + m_barThickness,         midY + drawHalfBar);

		const bool played = (barX + m_barThickness * 0.5f) <= playedX;
		draw->AddRectFilled(bp0, bp1, played ? m_playedColor : m_color);
	}
}
