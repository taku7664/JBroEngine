#include "pch.h"
#include "Time.h"

float CTime::GetDeltaSeconds() const
{
	return m_deltaSeconds;
}

float CTime::GetUnscaledDeltaSeconds() const
{
	return m_unscaledDeltaSeconds;
}

float CTime::GetElapsedSeconds() const
{
	return m_elapsedSeconds;
}

float CTime::GetUnscaledElapsedSeconds() const
{
	return m_unscaledElapsedSeconds;
}

float CTime::GetTimeScale() const
{
	return m_timeScale;
}

std::uint64_t CTime::GetFrameCount() const
{
	return m_frameCount;
}

void CTime::SetTimeScale(float timeScale)
{
	m_timeScale = timeScale < 0.0f ? 0.0f : timeScale;
}

float CTime::GetFixedDeltaSeconds() const
{
	return m_fixedDeltaSeconds;
}

void CTime::SetFixedDeltaSeconds(float fixedDelta)
{
	// Clamp to a sane range (1000 Hz min ~ 1 Hz max).
	if (fixedDelta < 0.001f) fixedDelta = 0.001f;
	if (fixedDelta > 1.0f)   fixedDelta = 1.0f;
	m_fixedDeltaSeconds = fixedDelta;
}

void CTime::Reset()
{
	m_lastFrameTime = std::chrono::steady_clock::now();
	m_deltaSeconds = 0.0f;
	m_unscaledDeltaSeconds = 0.0f;
	m_elapsedSeconds = 0.0f;
	m_unscaledElapsedSeconds = 0.0f;
	m_frameCount = 0;
	m_hasStarted = true;
}

void CTime::BeginFrame()
{
	const std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
	if (false == m_hasStarted)
	{
		m_lastFrameTime = currentTime;
		m_hasStarted = true;
	}

	m_unscaledDeltaSeconds = std::chrono::duration<float>(currentTime - m_lastFrameTime).count();
	m_deltaSeconds = m_unscaledDeltaSeconds * m_timeScale;
	m_unscaledElapsedSeconds += m_unscaledDeltaSeconds;
	m_elapsedSeconds += m_deltaSeconds;
	m_lastFrameTime = currentTime;
	++m_frameCount;
}
