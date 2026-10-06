#include "TestClock.h"

#include <JBro/Runtime/SystemContext.h>

#include <stdexcept>
#include <JBro/Types/Float.h>

namespace JBro::Testing
{
    System::TimeSystem& SharedClock()
    {
        static System::TimeSystem clock;
        return clock;
    }

    void AttachClock(FrameworkContext& context)
    {
        AttachClock(context, TimeSettings{});
    }

    void AttachClock(FrameworkContext& context, const TimeSettings& settings)
    {
        System::TimeSystem& clock = SharedClock();
        if (false == clock.Configure(settings))
        {
            throw std::runtime_error("the test clock settings must be valid");
        }
        clock.SetPaused(false);
        clock.SetTimeScale(1.0f);
        clock.ResetGameTime();
        SystemContext systems = GetSystemContext();
        systems.Time = &clock;
        BindSystemContext(systems);
        context.time = &clock;
    }

    void Tick(IFramework& framework, Float deltaTime)
    {
        if (false == SharedClock().BeginFrame(deltaTime))
        {
            throw std::runtime_error("a test frame must have a valid delta");
        }
        framework.Update();
    }
}
