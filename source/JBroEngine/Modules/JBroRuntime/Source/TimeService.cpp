#include <JBro/Runtime/TimeService.h>

#include <JBro/Runtime/SystemContext.h>

namespace JBro::Service
{
    namespace
    {
        // 묶인 시간 시스템이 없을 때의 시간이다. 델타 0, 타임스케일 1 인 멈춘 시간이다.
        const FrameTime& CurrentTime()
        {
            static const FrameTime Stopped{};
            const System::ITimeSystem* system = GetSystemContext().Time;
            return system != nullptr ? system->GetFrameTime() : Stopped;
        }
    }

    float TimeService::DeltaTime() const
    {
        const FrameTime& time = CurrentTime();
        return time.inFixedStep ? time.fixedDeltaTime : time.deltaTime;
    }

    float TimeService::UnscaledDeltaTime() const
    {
        return CurrentTime().unscaledDeltaTime;
    }

    float TimeService::FixedDeltaTime() const
    {
        return CurrentTime().fixedDeltaTime;
    }

    double TimeService::Time() const
    {
        const FrameTime& time = CurrentTime();
        return time.inFixedStep ? time.fixedTime : time.time;
    }

    double TimeService::UnscaledTime() const
    {
        return CurrentTime().unscaledTime;
    }

    double TimeService::FixedTime() const
    {
        return CurrentTime().fixedTime;
    }

    std::uint64_t TimeService::FrameCount() const
    {
        return CurrentTime().frameCount;
    }

    float TimeService::TimeScale() const
    {
        return CurrentTime().timeScale;
    }

    bool TimeService::SetTimeScale(float scale) const
    {
        System::ITimeSystem* system = GetSystemContext().Time;
        return system != nullptr && system->SetTimeScale(scale);
    }

    bool TimeService::IsInFixedStep() const
    {
        return CurrentTime().inFixedStep;
    }

    float TimeService::FixedStepAlpha() const
    {
        return CurrentTime().fixedStepAlpha;
    }

    bool TimeService::IsPaused() const
    {
        return CurrentTime().paused;
    }
}
