#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/SystemScheduler.h>
#include <JBro/Core/Profiler.h>

#include <cstring>
#include <JBro/Framework2DSystem/Framework2D.h>

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
            // 다른 테스트 파일과 같이 무엇이 어긋났는지 적고 던진다. 적지 않으면
            // 실행이 끝난 자리만 보이고 어느 줄에서 멈췄는지 알 수 없다.
            std::cout << "test failure: " << message << std::endl;
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
        auto* body = canvas->AttachComponent<JBro::Component::Rigidbody2D>(object);
        framework.Update(3.0f);
        Check(std::fabs(body->linearVelocity.y + 4.905f) < 0.0001f, "long frame must execute at most two physics steps");
        Check(std::fabs(transform->worldPosition.y - transform->position.y) < 0.0001f
            && transform->worldValid,
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

    // **어느 시스템이 느린지 보여야 한다**(D-194, 기존 CPU 프로파일러의 풀 목록).
    // 전에는 프레임이 `Systems` 한 덩어리라, 느려져도 어디가 느린지는 아무것도 말해
    // 주지 않았다. 시스템마다 한 줄이어야 하고, 그 이름은 그 시스템의 타입 이름이다.
    void TestEachSystemIsMeasuredUnderItsOwnName()
    {
        Trace trace;
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        // 시스템이 내려갈 때 캔버스에 오브젝트가 남아 있어야 한다(위 검사가 그것을 잰다).
        Check(canvas.CreateObject("lifetime probe") != nullptr, "probe object must be created");
        auto& systems = canvas.GetSystems();
        systems.AddSystem<ProbeSystem>(trace, 1, 100);
        systems.Initialize(canvas);

        JBro::Profiler::SetEnabled(true);
        JBro::Profiler::BeginFrame();
        systems.Update(canvas, 0.0f);
        systems.FixedUpdate(canvas, 0.1f);
        JBro::Profiler::EndFrame();

        // **끄기 전에 읽는다.** 끄는 것은 쌓인 프레임을 버리는 일이기도 하다
        // (지난 세기의 숫자가 다시 켰을 때 한 프레임 섞이지 않게).
        const JBro::ProfileSample* found = nullptr;
        for (std::size_t index = 0; index < JBro::Profiler::GetCount(); ++index)
        {
            const JBro::ProfileSample* sample = JBro::Profiler::GetAt(index);
            // **마지막 마디만 남아야 한다.** `class \`anonymous namespace'::ProbeSystem` 이
            // 그대로 창에 나오면 목록을 읽을 수 없다.
            if (sample != nullptr && sample->name != nullptr
                && std::strcmp(sample->name, "ProbeSystem") == 0)
            {
                found = sample;
            }
        }
        Check(found != nullptr, "the system must be measured under its own type name");
        // 두 번 돌았다(업데이트와 고정 스텝). 기존도 풀의 총 순회시간을 합해 냈고,
        // 몇 번 돌았는지는 부른 횟수가 말한다.
        Check(found->callCount == 2, "and both the update and the fixed step count towards it");

        // **꺼져 있으면 아무것도 쌓지 않는다.** 게임 실행이 재는 값이 아니다.
        JBro::Profiler::SetEnabled(false);
        JBro::Profiler::BeginFrame();
        systems.Update(canvas, 0.0f);
        JBro::Profiler::EndFrame();
        Check(JBro::Profiler::GetCount() == 0, "a frame measured while off must be empty");
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
    TestEachSystemIsMeasuredUnderItsOwnName();
    TestFrameworkFixedStepBudget();
    std::cout << "System scheduler tests passed.\n";
    return 0;
}
