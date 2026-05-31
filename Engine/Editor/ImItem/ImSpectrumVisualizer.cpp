#include "pch.h"
#include "ImSpectrumVisualizer.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float TAU = 6.28318530717958647692f;

	bool IsPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }
}

// ── ctor/dtor ───────────────────────────────────────────────────────────────
ImSpectrumVisualizer::ImSpectrumVisualizer()
{
	EnsureSizes();
}
ImSpectrumVisualizer::~ImSpectrumVisualizer() = default;

// ── 내부 버퍼 보장 ──────────────────────────────────────────────────────────
void ImSpectrumVisualizer::EnsureSizes()
{
	if (m_window.size() != static_cast<std::size_t>(m_fftSize))
	{
		m_window.resize(m_fftSize);
		for (int i = 0; i < m_fftSize; ++i)
		{
			// Hann window — 스펙트럼 누설 감소.
			m_window[i] = 0.5f * (1.0f - std::cos(TAU * static_cast<float>(i) / static_cast<float>(m_fftSize - 1)));
		}
	}
	m_real.assign(static_cast<std::size_t>(m_fftSize), 0.0f);
	m_imag.assign(static_cast<std::size_t>(m_fftSize), 0.0f);
	m_magnitudes.assign(static_cast<std::size_t>(m_fftSize / 2), 0.0f);

	m_barValues .assign(static_cast<std::size_t>(m_bars), 0.0f);
	m_peakValues.assign(static_cast<std::size_t>(m_bars), 0.0f);
}

// ── PCM bind/unbind ─────────────────────────────────────────────────────────
void ImSpectrumVisualizer::BindPcm(const void* data, std::size_t byteCount,
                                   std::uint32_t sampleRate, std::uint16_t channels,
                                   ESampleFormat fmt)
{
	m_pcm          = static_cast<const std::uint8_t*>(data);
	m_pcmByteCount = byteCount;
	m_sampleRate   = sampleRate;
	m_channels     = (channels > 0) ? channels : 1;
	m_format       = fmt;
	// 막대 상태는 리셋해서 새 자산 시작 시 0 부터 부드럽게 올라오게.
	std::fill(m_barValues .begin(), m_barValues .end(), 0.0f);
	std::fill(m_peakValues.begin(), m_peakValues.end(), 0.0f);
}

void ImSpectrumVisualizer::Unbind()
{
	m_pcm          = nullptr;
	m_pcmByteCount = 0;
	std::fill(m_barValues .begin(), m_barValues .end(), 0.0f);
	std::fill(m_peakValues.begin(), m_peakValues.end(), 0.0f);
}

// ── chain setter ────────────────────────────────────────────────────────────
ImSpectrumVisualizer& ImSpectrumVisualizer::SetBarCount(int bars)
{
	const int clamped = std::max(1, std::min(512, bars));
	if (clamped != m_bars)
	{
		m_bars = clamped;
		m_barValues .assign(static_cast<std::size_t>(m_bars), 0.0f);
		m_peakValues.assign(static_cast<std::size_t>(m_bars), 0.0f);
	}
	return *this;
}
ImSpectrumVisualizer& ImSpectrumVisualizer::SetBarGap         (float g) { m_barGap = std::max(0.0f, g);             return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetSmoothingFactor(float k) { m_smoothing = std::max(0.0f, std::min(0.99f, k)); return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetPeakDecay      (float v) { m_peakDecay = std::max(0.0f, v);          return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetMinFreq        (float v) { m_minFreq = std::max(1.0f, v);            return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetMaxFreq        (float v) { m_maxFreq = std::max(m_minFreq + 1.0f, v);return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetBackgroundColor(ImU32 c) { m_bgColor = c;                            return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetColorLow       (ImU32 c) { m_colorLow = c;                           return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetColorMid       (ImU32 c) { m_colorMid = c;                           return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetColorHigh      (ImU32 c) { m_colorHigh = c;                          return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetShowPeakMarkers(bool sh) { m_showPeakMarkers = sh;                   return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetRounding       (float r) { m_rounding = std::max(0.0f, r);           return *this; }
ImSpectrumVisualizer& ImSpectrumVisualizer::SetFftSize(int powerOfTwo)
{
	if (IsPowerOfTwo(powerOfTwo) && powerOfTwo >= 128 && powerOfTwo <= 8192 && powerOfTwo != m_fftSize)
	{
		m_fftSize = powerOfTwo;
		EnsureSizes();
	}
	return *this;
}

// ── 작은 radix-2 Cooley-Tukey FFT (in-place) ────────────────────────────────
void ImSpectrumVisualizer::FFT(float* re, float* im, int N)
{
	// Bit-reversal permutation
	int j = 0;
	for (int i = 1; i < N; ++i)
	{
		int bit = N >> 1;
		for (; (j & bit) != 0; bit >>= 1)
		{
			j ^= bit;
		}
		j ^= bit;
		if (i < j)
		{
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	// Butterflies
	for (int len = 2; len <= N; len <<= 1)
	{
		const float ang  = -TAU / static_cast<float>(len);
		const float wnRe = std::cos(ang);
		const float wnIm = std::sin(ang);
		const int   half = len >> 1;

		for (int i = 0; i < N; i += len)
		{
			float wRe = 1.0f, wIm = 0.0f;
			for (int k = 0; k < half; ++k)
			{
				const float aRe = re[i + k];
				const float aIm = im[i + k];
				const float bRe = re[i + k + half] * wRe - im[i + k + half] * wIm;
				const float bIm = re[i + k + half] * wIm + im[i + k + half] * wRe;
				re[i + k]        = aRe + bRe;
				im[i + k]        = aIm + bIm;
				re[i + k + half] = aRe - bRe;
				im[i + k + half] = aIm - bIm;
				// w *= wn
				const float nwRe = wRe * wnRe - wIm * wnIm;
				const float nwIm = wRe * wnIm + wIm * wnRe;
				wRe = nwRe;
				wIm = nwIm;
			}
		}
	}
}

// ── 한 윈도우 FFT 굴리기 ────────────────────────────────────────────────────
void ImSpectrumVisualizer::RunFFT(std::size_t centerFrame)
{
	const std::size_t bytesPerSample = (ESampleFormat::S16 == m_format) ? 2u : 4u;
	const std::size_t bytesPerFrame  = bytesPerSample * m_channels;
	if (0 == bytesPerFrame) return;

	const std::size_t totalFrames = m_pcmByteCount / bytesPerFrame;

	const std::size_t halfN     = static_cast<std::size_t>(m_fftSize / 2);
	const std::size_t startFrame = (centerFrame >= halfN) ? (centerFrame - halfN) : 0;

	// 윈도우 채우기 — 채널 평균 mono, Hann window 적용
	for (int i = 0; i < m_fftSize; ++i)
	{
		const std::size_t f = startFrame + static_cast<std::size_t>(i);
		float mono = 0.0f;
		if (f < totalFrames)
		{
			const std::uint8_t* fp = m_pcm + f * bytesPerFrame;
			for (std::uint16_t ch = 0; ch < m_channels; ++ch)
			{
				float s = 0.0f;
				if (ESampleFormat::S16 == m_format)
				{
					s = static_cast<float>(*reinterpret_cast<const std::int16_t*>(fp + ch * 2)) / 32768.0f;
				}
				else
				{
					s = *reinterpret_cast<const float*>(fp + ch * 4);
				}
				mono += s;
			}
			mono /= static_cast<float>(m_channels);
		}
		m_real[i] = mono * m_window[i];
		m_imag[i] = 0.0f;
	}

	FFT(m_real.data(), m_imag.data(), m_fftSize);

	// 진폭 = sqrt(re² + im²) * 2/N — N 분의 1 의 amplitude 스케일
	const float scale = 2.0f / static_cast<float>(m_fftSize);
	for (std::size_t k = 0; k < halfN; ++k)
	{
		const float re = m_real[k];
		const float im = m_imag[k];
		m_magnitudes[k] = std::sqrt(re * re + im * im) * scale;
	}
}

// ── Tick — 막대값/피크 갱신 ─────────────────────────────────────────────────
void ImSpectrumVisualizer::Tick(double currentSeconds, float deltaSeconds)
{
	deltaSeconds = std::max(0.0f, deltaSeconds);

	if (nullptr == m_pcm || 0 == m_channels || 0 == m_sampleRate)
	{
		// PCM 미바인드/정지 — 막대는 자연스럽게 가라앉도록 부드럽게 감쇠만 한다.
		const float k = m_smoothing;
		const float decay = m_peakDecay * deltaSeconds;
		for (int b = 0; b < m_bars; ++b)
		{
			m_barValues[b]  = k * m_barValues[b];
			m_peakValues[b] = std::max(0.0f, m_peakValues[b] - decay);
		}
		return;
	}

	const std::int64_t centerFrame = static_cast<std::int64_t>(currentSeconds * static_cast<double>(m_sampleRate));
	RunFFT(static_cast<std::size_t>(std::max<std::int64_t>(0, centerFrame)));

	// 막대 인덱스 → 주파수 [m_minFreq, m_maxFreq] 의 log 매핑
	const float binHz  = static_cast<float>(m_sampleRate) / static_cast<float>(m_fftSize);
	const float logMin = std::log(m_minFreq);
	const float logMax = std::log(m_maxFreq);
	const int   halfN  = m_fftSize / 2;

	for (int b = 0; b < m_bars; ++b)
	{
		const float t0 = static_cast<float>(b)     / static_cast<float>(m_bars);
		const float t1 = static_cast<float>(b + 1) / static_cast<float>(m_bars);
		const float f0 = std::exp(logMin + (logMax - logMin) * t0);
		const float f1 = std::exp(logMin + (logMax - logMin) * t1);
		int bin0 = std::max(1, static_cast<int>(std::floor(f0 / binHz)));
		int bin1 = std::max(bin0 + 1, static_cast<int>(std::ceil(f1 / binHz)));
		if (bin1 > halfN) bin1 = halfN;
		if (bin0 >= bin1) { bin1 = std::min(bin0 + 1, halfN); if (bin0 >= bin1) continue; }

		float maxMag = 0.0f;
		for (int k = bin0; k < bin1; ++k)
		{
			if (m_magnitudes[k] > maxMag) maxMag = m_magnitudes[k];
		}

		// 진폭 → 화면 높이로 로그 압축.  값이 작은 신호도 보이게.
		float val = std::log10(1.0f + maxMag * 50.0f) / std::log10(51.0f);
		val = std::max(0.0f, std::min(1.0f, val));

		// EMA 스무딩 — 천천히 변하는 막대
		m_barValues[b] = m_smoothing * m_barValues[b] + (1.0f - m_smoothing) * val;

		// 피크 홀드 + 감쇠
		if (val > m_peakValues[b])
		{
			m_peakValues[b] = val;
		}
		else
		{
			m_peakValues[b] = std::max(0.0f, m_peakValues[b] - m_peakDecay * deltaSeconds);
		}
	}
}

// ── 그리기 ──────────────────────────────────────────────────────────────────
namespace
{
	ImU32 LerpColor(ImU32 a, ImU32 b, float t)
	{
		t = std::max(0.0f, std::min(1.0f, t));
		const int aR = (a >> IM_COL32_R_SHIFT) & 0xff;
		const int aG = (a >> IM_COL32_G_SHIFT) & 0xff;
		const int aB = (a >> IM_COL32_B_SHIFT) & 0xff;
		const int aA = (a >> IM_COL32_A_SHIFT) & 0xff;
		const int bR = (b >> IM_COL32_R_SHIFT) & 0xff;
		const int bG = (b >> IM_COL32_G_SHIFT) & 0xff;
		const int bB = (b >> IM_COL32_B_SHIFT) & 0xff;
		const int bA = (b >> IM_COL32_A_SHIFT) & 0xff;
		const int r = aR + static_cast<int>((bR - aR) * t);
		const int g = aG + static_cast<int>((bG - aG) * t);
		const int bl = aB + static_cast<int>((bB - aB) * t);
		const int al = aA + static_cast<int>((bA - aA) * t);
		return IM_COL32(r, g, bl, al);
	}
}

void ImSpectrumVisualizer::operator()(const ImVec2& size)
{
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float  w = (size.x < 0.0f) ? avail.x : size.x;
	const float  h = (size.y <= 0.0f) ? 80.0f  : size.y;
	if (w < 1.0f || h < 1.0f) return;

	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const ImVec2 p1(p0.x + w, p0.y + h);
	ImDrawList*  dl = ImGui::GetWindowDrawList();

	ImGui::Dummy(ImVec2(w, h));

	if (0 != m_bgColor)
	{
		dl->AddRectFilled(p0, p1, m_bgColor);
	}

	if (m_bars <= 0) return;

	const float totalGap   = m_barGap * static_cast<float>(m_bars - 1);
	const float barWidth   = (w - totalGap) / static_cast<float>(m_bars);
	if (barWidth < 0.5f) return;

	for (int i = 0; i < m_bars; ++i)
	{
		const float val  = m_barValues [i];
		const float peak = m_peakValues[i];

		// 진폭에 따른 색 그라데이션: low → mid → high
		ImU32 col;
		if (val < 0.5f)
		{
			col = LerpColor(m_colorLow, m_colorMid, val * 2.0f);
		}
		else
		{
			col = LerpColor(m_colorMid, m_colorHigh, (val - 0.5f) * 2.0f);
		}

		const float x      = p0.x + static_cast<float>(i) * (barWidth + m_barGap);
		const float barH   = val * h;
		const float barTop = p1.y - barH;
		const float r      = std::min(m_rounding, barWidth * 0.4f);

		// 막대 본체.
		dl->AddRectFilled(ImVec2(x, barTop), ImVec2(x + barWidth, p1.y), col, r);

		// 피크 마커 — 막대 위에 떠 있는 작은 가로 막대.
		if (m_showPeakMarkers && peak > val + 0.01f)
		{
			const float peakY = p1.y - peak * h;
			const ImU32 markerCol = IM_COL32(255, 255, 255, 220);
			dl->AddRectFilled(
				ImVec2(x,                peakY - 1.5f),
				ImVec2(x + barWidth,     peakY + 0.5f),
				markerCol);
		}
	}
}
