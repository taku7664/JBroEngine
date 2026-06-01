#pragma once

#include "Utillity/Pointer/SafePtr.h"

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
	float GetFixedDeltaSeconds() const;
	std::uint64_t GetFrameCount() const;

	void SetTimeScale(float timeScale);
	void SetFixedDeltaSeconds(float fixedDelta);
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
	float m_fixedDeltaSeconds = 0.02f; // 50 Hz default
	std::uint64_t m_frameCount = 0;
	bool m_hasStarted = false;
};
