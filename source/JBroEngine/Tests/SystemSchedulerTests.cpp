#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Runtime/SystemScheduler.h>
#include <JBro/Framework2D/Framework2D.h>

#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char* message)
    {
        if (false == value)
        {
            throw std::runtime_error(message);
        }
    }

    struct Trace
    {
        int events[32]{};
        int count = 0;
    };

    class ProbeSystem final : public JBro::GameSystem
    {
    public:
        ProbeSystem(Trace& trace, int id, int order)
            : m_trace(trace), m_id(id), m_order(order)
        {
        }

        int GetExecutionOrder() const override
        {
            return m_order;
        }

    protected:
        void OnInitialize(JBro::Canvas&) override
        {
            m_trace.events[m_trace.count++] = m_id;
        }
        void OnUpdate(JBro::Canvas&, float) override
        {
            m_trace.events[m_trace.count++] = 10 + m_id;
        }
        void OnFixedUpdate(JBro::Canvas&, float) override
        {
            m_trace.events[m_trace.count++] = 20 + m_id;
        }
        void OnShutdown(JBro::Canvas& canvas) override
        {
            Check(canvas.GetObjectCount() == 1, "systems must shut down while canvas objects still exist");
            m_trace.events[m_trace.count++] = 30 + m_id;
        }

    private:
        Trace& m_trace;
        int m_id;
        int m_order;
    };

    void TestFrameworkFixedStepBudget()
    {
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        context.fixedDeltaTime = 0.0f;
        Check(false == framework.Initialize(context), "zero fixed step must be rejected");
        context.fixedDeltaTime = (std::numeric_limits<float>::infinity)();
        Check(false == framework.Initialize(context), "nonfinite fixed step must be rejected");
        context.fixedDeltaTime = 0.25f;
        context.maxFixedStepsPerFrame = 2;
        Check(framework.Initialize(context), "valid fixed step configuration must initialize");
        auto* canvas = framework.GetCanvas();
        auto* object = canvas->CreateObject("body");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        auto* world = canvas->AttachComponent<JBro::Component::WorldTransform2D>(object);
        auto* body = canvas->AttachComponent<JBro::Component::Rigidbody2D>(object);
        framework.Update(3.0f);
        Check(std::fabs(body->linearVelocity.y + 4.905f) < 0.0001f, "long frame must execute at most two physics steps");
        Check(std::fabs(world->position.y - transform->position.y) < 0.0001f && false == world->dirty,
            "transform propagation must follow the fixed physics steps");
        const float position = transform->position.y;
        framework.Update(0.125f);
        Check(transform->position.y == position, "excess whole-step debt must not leak into later frames");
        framework.Update((std::numeric_limits<float>::quiet_NaN)());
        Check(transform->position.y == position, "invalid frame time must not enter the physics accumulator");
        framework.Shutdown();
        Check(framework.Initialize(context), "framework must reopen");
        canvas = framework.GetCanvas();
        object = canvas->CreateObject("reopened body");
        canvas->AttachComponent<JBro::Component::Transform2D>(object);
        body = canvas->AttachComponent<JBro::Component::Rigidbody2D>(object);
        framework.Update(0.125f);
        Check(body->linearVelocity.y == 0.0f, "project reopening must reset the fractional fixed-step accumulator");
    }

    void TestCanvasOwnsOrderedSystems()
    {
        Trace trace;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            Check(canvas.CreateObject("lifetime probe") != nullptr, "probe object must be created");
            auto& systems = canvas.GetSystems();
            Check(systems.FindSystem<ProbeSystem>() == nullptr, "empty scheduler must not find a system");
            auto& late = systems.AddSystem<ProbeSystem>(trace, 2, 200);
            auto& early = systems.AddSystem<ProbeSystem>(trace, 1, 100);
            systems.Update(canvas, 0.0f);
            Check(trace.count == 0, "uninitialized scheduler must not update systems");
            systems.Initialize(canvas);
            Check(trace.count == 2 && trace.events[0] == 1 && trace.events[1] == 2,
                "systems must initialize in execution order");
            Check(systems.FindSystem<ProbeSystem>() == &early && systems.GetSystem(1) == &late,
                "typed and indexed lookup must find registered systems without RTTI");
            Check(systems.GetSystem(2) == nullptr, "out-of-range system access must fail");
            systems.Initialize(canvas);
            Check(trace.count == 2, "repeat initialization must not invoke hooks twice");
            bool rejected = false;
            try
            {
                systems.AddSystem<ProbeSystem>(trace, 3, 300);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            Check(rejected && systems.GetSystemCount() == 2, "live schedule mutation must be rejected");
            systems.Update(canvas, 0.0f);
            Check(trace.events[2] == 11 && trace.events[3] == 12, "update order must match initialization order");
            early.SetEnabled(false);
            systems.FixedUpdate(canvas, 0.1f);
            Check(trace.count == 5 && trace.events[4] == 22, "disabled systems must not run fixed updates");
        }
        Check(trace.count == 7 && trace.events[5] == 32 && trace.events[6] == 31,
            "canvas destruction must shut systems down once in reverse order");
    }
}

int RunSystemSchedulerTests()
{
    TestCanvasOwnsOrderedSystems();
    TestFrameworkFixedStepBudget();
    std::cout << "System scheduler tests passed.\n";
    return 0;
}
