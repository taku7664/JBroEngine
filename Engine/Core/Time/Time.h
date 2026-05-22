#pragma once

#include "Utillity/SafePtr.h"

#include <chrono>
#include <cstdint>

class CEngine;

class CTime final : public EnableSafeFromThis<CTime>
{
	friend class CEngine;

public:
	CTime() = default;
	~CTime() = default;
	CTime(const CTime&) = delete;
	CTime& operator=(const CTime&) = delete;
	CTime(CTime&&) = delete;
	CTime& operator=(CTime&&) = delete;

public:
	float GetDeltaSeconds() const;
	float GetUnscaledDeltaSeconds() const;
	float GetElapsedSeconds() const;
	float GetUnscaledElapsedSeconds() const;
	float GetTimeScale() const;
	std::uint64_t GetFrameCount() const;

	void SetTimeScale(float timeScale);
	void Reset();

private:
	void BeginFrame();

private:
	std::chrono::steady_clock::time_point m_lastFrameTime{};
	float m_deltaSeconds = 0.0f;
	float m_unscaledDeltaSeconds = 0.0f;
	float m_elapsedSeconds = 0.0f;
	float m_unscaledElapsedSeconds = 0.0f;
	float m_timeScale = 1.0f;
	std::uint64_t m_frameCount = 0;
	bool m_hasStarted = false;
};
