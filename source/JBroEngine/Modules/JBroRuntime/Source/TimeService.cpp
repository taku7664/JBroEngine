#include <JBro/Runtime/TimeService.h>

#include <JBro/Runtime/SystemContext.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

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

    Float TimeService::DeltaTime() const
    {
        const FrameTime& time = CurrentTime();
        return time.inFixedStep ? time.fixedDeltaTime : time.deltaTime;
    }

    Float TimeService::UnscaledDeltaTime() const
    {
        return CurrentTime().unscaledDeltaTime;
    }

    Float TimeService::FixedDeltaTime() const
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

    UInt64 TimeService::FrameCount() const
    {
        return CurrentTime().frameCount;
    }

    Float TimeService::TimeScale() const
    {
        return CurrentTime().timeScale;
    }

    Bool TimeService::SetTimeScale(Float scale) const
    {
        System::ITimeSystem* system = GetSystemContext().Time;
        return system != nullptr && system->SetTimeScale(scale);
    }

    Bool TimeService::IsInFixedStep() const
    {
        return CurrentTime().inFixedStep;
    }

    Float TimeService::FixedStepAlpha() const
    {
        return CurrentTime().fixedStepAlpha;
    }

    Bool TimeService::IsPaused() const
    {
        return CurrentTime().paused;
    }
}
