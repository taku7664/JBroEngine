#include <JBro/Host/TimeSystem.h>

#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    Bool TimeSystem::IsValid(const TimeSettings& settings)
    {
        return std::isfinite(settings.fixedDeltaTime) && settings.fixedDeltaTime >= 0.001f && settings.fixedDeltaTime <= 1.0f
            && settings.maxFixedSteps >= 1 && settings.maxFixedSteps <= 64
            && std::isfinite(settings.maxDeltaTime) && settings.maxDeltaTime > 0.0f && settings.maxDeltaTime <= 10.0f;
    }

    Bool TimeSystem::Configure(const TimeSettings& settings)
    {
        if (false == IsValid(settings))
        {
            return false;
        }
        m_settings = settings;
        m_time.fixedDeltaTime = settings.fixedDeltaTime;
        // 스텝이 짧아졌으면 남은 누산이 한 스텝을 넘을 수 있다. 그 몫은 다음 프레임의 스텝이 된다 - 버리지 않는다.
        return true;
    }

    const TimeSettings& TimeSystem::GetSettings() const
    {
        return m_settings;
    }

    Bool TimeSystem::BeginFrame(Float rawDeltaTime)
    {
        if (false == std::isfinite(rawDeltaTime) || rawDeltaTime < 0.0f)
        {
            return false;
        }
        const Float unscaled = rawDeltaTime > m_settings.maxDeltaTime ? m_settings.maxDeltaTime : rawDeltaTime;
        const double fixed = m_settings.fixedDeltaTime;
        ++m_time.frameCount;
        m_time.unscaledDeltaTime = unscaled;
        m_time.unscaledTime += unscaled;
        m_time.fixedDeltaTime = m_settings.fixedDeltaTime;
        m_time.inFixedStep = false;
        m_time.stepFrame = m_time.paused && m_stepRequested;
        m_stepRequested = false;

        double delta = 0.0;
        UInt32 steps = 0;
        if (m_time.stepFrame)
        {
            // 한 프레임 진행은 누산기를 건드리지 않고 정확히 한 스텝이다. 게임 시간도 그만큼만 간다.
            delta = fixed;
            steps = 1;
        }
        else if (false == m_time.paused)
        {
            delta = static_cast<double>(unscaled) * m_time.timeScale;
            m_accumulator += delta;
            double whole = std::floor(m_accumulator / fixed);
            if (whole > m_settings.maxFixedSteps)
            {
                // 상한을 넘은 스텝은 돌지 않는다. 그만큼 게임 시간도 흐르지 않은 것으로 쳐서 `Time()` 과 `FixedTime()` 이 어긋나지 않게 한다.
                const double dropped = (whole - m_settings.maxFixedSteps) * fixed;
                m_accumulator -= dropped;
                delta -= dropped;
                whole = m_settings.maxFixedSteps;
            }
            steps = static_cast<JBro::UInt32>(whole);
            m_accumulator -= whole * fixed;
            if (m_accumulator < 0.0)
            {
                m_accumulator = 0.0;
            }
        }
        m_time.deltaTime = static_cast<JBro::Float>(delta);
        m_time.time += delta;
        m_time.fixedStepCount = steps;
        m_time.fixedStepAlpha = static_cast<JBro::Float>(m_accumulator / fixed);
        if (m_time.fixedStepAlpha >= 1.0f)
        {
            m_time.fixedStepAlpha = std::nextafter(1.0f, 0.0f);
        }
        return true;
    }

    void TimeSystem::BeginFixedStep()
    {
        m_time.inFixedStep = true;
        m_time.fixedTime += m_settings.fixedDeltaTime;
    }

    void TimeSystem::EndFixedSteps()
    {
        m_time.inFixedStep = false;
    }

    void TimeSystem::SetPaused(Bool paused)
    {
        m_time.paused = paused;
        if (false == paused)
        {
            m_stepRequested = false;
        }
    }

    Bool TimeSystem::IsPaused() const
    {
        return m_time.paused;
    }

    void TimeSystem::RequestStep()
    {
        if (m_time.paused)
        {
            m_stepRequested = true;
        }
    }

    Bool TimeSystem::IsStepFrame() const
    {
        return m_time.stepFrame;
    }

    Bool TimeSystem::IsSimulating() const
    {
        return false == m_time.paused || m_time.stepFrame;
    }

    void TimeSystem::ResetGameTime()
    {
        m_time.time = 0.0;
        m_time.fixedTime = 0.0;
        m_time.deltaTime = 0.0f;
        m_time.fixedStepCount = 0;
        m_time.fixedStepAlpha = 0.0f;
        m_time.timeScale = 1.0f;
        m_time.inFixedStep = false;
        m_time.stepFrame = false;
        m_accumulator = 0.0;
        m_stepRequested = false;
    }

    const FrameTime& TimeSystem::GetFrameTime() const
    {
        return m_time;
    }

    Bool TimeSystem::SetTimeScale(Float scale)
    {
        if (false == std::isfinite(scale) || scale < 0.0f || scale > 100.0f)
        {
            return false;
        }
        m_time.timeScale = scale;
        return true;
    }
}
