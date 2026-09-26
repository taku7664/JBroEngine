#include "TestClock.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Core/RandomStream.h>
#include <JBro/Host/RandomSystem.h>
#include <JBro/Host/TimeSystem.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    bool Near(double left, double right, double tolerance = 1e-6)
    {
        return std::fabs(left - right) <= tolerance;
    }

    // 공통 시스템 컨텍스트를 이 검사 동안만 묶는다.
    class BoundSystems
    {
    public:
        BoundSystems(JBro::System::ITimeSystem* time, JBro::System::IRandomSystem* random)
        {
            JBro::SystemContext context;
            context.Time = time;
            context.Random = random;
            JBro::BindSystemContext(context);
        }
        ~BoundSystems()
        {
            JBro::BindSystemContext({});
        }
        BoundSystems(const BoundSystems&) = delete;
        BoundSystems& operator=(const BoundSystems&) = delete;
    };

    // ── RandomStream ─────────────────────────────────────────────────────

    // PCG 공개 데모(`pcg32-demo`, 씨앗 42·흐름 54)의 첫 여섯 수다. 이것이 맞으면 상태 전이와 출력 함수가 원전과 같다.
    void TestStreamMatchesTheReferenceSequence()
    {
        JBro::RandomStream stream(42u, 54u);
        const std::uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
        for (std::uint32_t value : expected)
        {
            Check(stream.NextUInt32() == value, "PCG32 must reproduce the reference demo sequence for seed 42, stream 54");
        }
    }

    void TestStreamsAreDeterministicAndSeparate()
    {
        JBro::RandomStream first(7u);
        JBro::RandomStream second(7u);
        JBro::RandomStream otherStream(7u, 1u);
        bool differs = false;
        for (int i = 0; i < 16; ++i)
        {
            const std::uint32_t a = first.NextUInt32();
            Check(a == second.NextUInt32(), "the same seed must give the same numbers");
            differs = differs || a != otherStream.NextUInt32();
        }
        Check(differs, "another stream number must give another sequence");

        const JBro::RandomState saved = first.GetState();
        std::uint32_t drawn[5];
        for (std::uint32_t& value : drawn)
        {
            value = first.NextUInt32();
        }
        first.SetState(saved);
        for (std::uint32_t value : drawn)
        {
            Check(first.NextUInt32() == value, "restoring the state must replay the same numbers");
        }
        JBro::RandomStream evenIncrement;
        evenIncrement.SetState({123u, 10u});
        Check((evenIncrement.GetState().increment & 1u) == 1u, "an even increment must be made odd");
    }

    void TestIntegerRangeCoversBothEnds()
    {
        JBro::RandomStream stream(99u);
        bool seen[5] = {};
        for (int i = 0; i < 10000; ++i)
        {
            const std::int32_t value = stream.Range(3, 7);
            Check(value >= 3 && value <= 7, "Range(3, 7) must stay inside 3..7");
            seen[value - 3] = true;
        }
        for (bool hit : seen)
        {
            Check(hit, "every value of 3..7 must come up, both ends included");
        }
        for (int i = 0; i < 1000; ++i)
        {
            const std::int32_t swapped = stream.Range(7, 3);
            Check(swapped >= 3 && swapped <= 7, "a reversed range must be swapped, not fail");
        }
        Check(stream.Range(5, 5) == 5, "an empty width is its one value");
        const std::int32_t low = std::numeric_limits<std::int32_t>::min();
        const std::int32_t high = std::numeric_limits<std::int32_t>::max();
        bool negative = false;
        bool positive = false;
        for (int i = 0; i < 64; ++i)
        {
            const std::int32_t value = stream.Range(low, high);
            negative = negative || value < 0;
            positive = positive || value > 0;
        }
        Check(negative && positive, "the full 32-bit range must use both halves");
    }

    // 치우침이 없어야 한다. 0..9 를 10 만 번 뽑아 카이제곱(자유도 9)을 잰다. 27.9 는 p = 0.001 의 경계다.
    void TestIntegerRangeIsUnbiased()
    {
        JBro::RandomStream stream(2026u);
        std::uint32_t counts[10] = {};
        constexpr int Draws = 100000;
        for (int i = 0; i < Draws; ++i)
        {
            ++counts[stream.Range(0, 9)];
        }
        double chiSquare = 0.0;
        for (std::uint32_t count : counts)
        {
            const double difference = static_cast<double>(count) - Draws / 10.0;
            chiSquare += difference * difference / (Draws / 10.0);
        }
        std::cout << "  chi-square of 100000 draws over 0..9: " << chiSquare << std::endl;
        Check(chiSquare < 27.9, "the integer range must not favour any value");
    }

    // 곱셈 거절이 실제로 다시 뽑아야 한다. 폭 3 의 문턱은 1 이라, 곱의 아래 32 비트가 0 인 첫 수(0)는 버리고 다음 수(1)로 0 을 낸다.
    void TestBoundedRejectsTheBiasedZone()
    {
        struct Scripted
        {
            std::uint32_t values[2] = {0u, 1u};
            std::uint32_t next = 0;
            std::uint32_t operator()()
            {
                return values[next++];
            }
        };
        Scripted source;
        Check(JBro::RandomMapping::Bounded(source, 3u) == 0u, "the rejected draw must be replaced, giving 0 from the second one");
        Check(source.next == 2, "a draw inside the biased zone must be thrown away and drawn again");
    }

    void TestFloatRangeIsHalfOpen()
    {
        JBro::RandomStream stream(5u);
        for (int i = 0; i < 10000; ++i)
        {
            const float value = stream.Value();
            Check(value >= 0.0f && value < 1.0f, "Value() must stay in [0, 1)");
            const float ranged = stream.Range(1.0f, 2.0f);
            Check(ranged >= 1.0f && ranged < 2.0f, "Range(1, 2) must stay in [1, 2)");
            const float swapped = stream.Range(2.0f, 1.0f);
            Check(swapped >= 1.0f && swapped < 2.0f, "a reversed float range must be swapped");
        }
        Check(stream.Range(2.0f, 2.0f) == 2.0f, "an empty float range is its one value");

        // 위 24 비트가 모두 1 이면 값은 1 - 2^-24 이다. 한 ulp 폭에 곱하면 반올림이 max 에 닿는다 - 그때 끝을 열어 둔다.
        struct AllOnes
        {
            std::uint32_t operator()()
            {
                return 0xFFFFFFFFu;
            }
        };
        AllOnes ones;
        Check(JBro::RandomMapping::Value(ones) < 1.0f, "the largest draw must stay below one");
        const float max = std::nextafter(1.0f, 2.0f);
        Check(JBro::RandomMapping::RangeFloat(ones, 1.0f, max) < max, "a draw rounded up to max must be pulled back below it");
    }

    void TestChance()
    {
        JBro::RandomStream stream(11u);
        int hits = 0;
        for (int i = 0; i < 100000; ++i)
        {
            Check(false == stream.Chance(0.0f), "a zero chance is never true");
            Check(stream.Chance(1.0f), "a chance of one is always true");
            hits += stream.Chance(0.25f) ? 1 : 0;
        }
        Check(hits > 24000 && hits < 26000, "a quarter chance must come up about a quarter of the time");
    }

    // ── TimeSystem ───────────────────────────────────────────────────────

    constexpr float Sixtieth = 1.0f / 60.0f;

    void TestOneFrameIsOneFixedStep()
    {
        JBro::System::TimeSystem time;
        Check(time.BeginFrame(Sixtieth), "a finite delta must open the frame");
        const JBro::FrameTime& frame = time.GetFrameTime();
        Check(Near(frame.deltaTime, Sixtieth), "the game delta is the raw delta at scale one");
        Check(frame.fixedStepCount == 1, "one sixtieth of a second is one step of one sixtieth");
        Check(frame.frameCount == 1, "the frame must be counted");
        Check(frame.fixedStepAlpha >= 0.0f && frame.fixedStepAlpha < 1.0f, "the alpha stays below one");
    }

    void TestFractionsCarryOver()
    {
        JBro::System::TimeSystem time;
        JBro::TimeSettings settings;
        settings.fixedDeltaTime = 0.02f;
        Check(time.Configure(settings), "a 0.02 s step is valid");
        // 0.035 초 프레임: 1 스텝(남음 0.015) → 2 스텝(0.05, 남음 0.01) → 2 스텝(0.045, 남음 0.005). 스텝의 배수에 닿지 않는 값이라
        // float 반올림이 경계를 흔들지 않는다.
        time.BeginFrame(0.035f);
        Check(time.GetFrameTime().fixedStepCount == 1, "0.035 s is one step of 0.02 s");
        Check(Near(time.GetFrameTime().fixedStepAlpha, 0.75, 1e-4), "the alpha must be the remainder over one step");
        time.BeginFrame(0.035f);
        Check(time.GetFrameTime().fixedStepCount == 2, "the remainder of a frame must count towards the next one");
        time.BeginFrame(0.035f);
        Check(time.GetFrameTime().fixedStepCount == 2, "and keep counting");
        Check(Near(time.GetFrameTime().fixedStepAlpha, 0.25, 1e-4), "the alpha follows the remainder");
    }

    void TestLongFramesAreClampedAndDroppedStepsStopTheClock()
    {
        JBro::System::TimeSystem time;
        time.BeginFrame(5.0f);
        const JBro::FrameTime& frame = time.GetFrameTime();
        Check(Near(frame.unscaledDeltaTime, 0.25), "a five-second hitch must be cut to the 0.25 s ceiling");
        Check(frame.fixedStepCount == 4, "at most four steps run in one frame");
        Check(frame.deltaTime < 5.0f * Sixtieth, "the steps that did not run must not count as game time");
        for (std::uint32_t i = 0; i < frame.fixedStepCount; ++i)
        {
            time.BeginFixedStep();
        }
        time.EndFixedSteps();
        Check(Near(frame.time, frame.fixedTime + frame.fixedStepAlpha * static_cast<double>(Sixtieth), 1e-5),
            "game time must be fixed time plus the remainder - they must not drift apart after a hitch");
    }

    void TestTimeScale()
    {
        JBro::System::TimeSystem time;
        Check(false == time.SetTimeScale(-1.0f), "a negative scale is refused");
        Check(false == time.SetTimeScale(std::numeric_limits<float>::quiet_NaN()), "NaN is refused");
        Check(false == time.SetTimeScale(101.0f), "a scale above 100 is refused");
        Check(time.GetFrameTime().timeScale == 1.0f, "a refused scale leaves the old one");
        Check(time.SetTimeScale(0.5f), "half speed is accepted");
        time.BeginFrame(Sixtieth);
        Check(Near(time.GetFrameTime().deltaTime, Sixtieth * 0.5), "half speed halves the game delta");
        Check(Near(time.GetFrameTime().unscaledDeltaTime, Sixtieth), "but not the unscaled one");
        Check(time.GetFrameTime().fixedStepCount == 0, "half a step does not run yet");
        time.BeginFrame(Sixtieth);
        Check(time.GetFrameTime().fixedStepCount == 1, "the second half completes the step - the step length does not change");
        Check(time.SetTimeScale(0.0f), "zero is accepted");
        time.BeginFrame(1.0f);
        Check(time.GetFrameTime().deltaTime == 0.0f && time.GetFrameTime().fixedStepCount == 0, "a zero scale stops game time");
    }

    void TestPauseAndStep()
    {
        JBro::System::TimeSystem time;
        time.BeginFrame(Sixtieth);
        time.RequestStep();
        time.BeginFrame(Sixtieth);
        Check(false == time.IsStepFrame(), "a step request while running is ignored");

        time.SetPaused(true);
        const double before = time.GetFrameTime().time;
        const double realBefore = time.GetFrameTime().unscaledTime;
        time.BeginFrame(Sixtieth);
        Check(time.GetFrameTime().deltaTime == 0.0f && time.GetFrameTime().fixedStepCount == 0, "a paused frame has no game time");
        Check(time.GetFrameTime().time == before, "game time stands still while paused");
        Check(time.GetFrameTime().unscaledTime > realBefore, "real time keeps going");
        Check(false == time.IsSimulating(), "a paused frame does not simulate");

        time.RequestStep();
        time.BeginFrame(0.5f);
        Check(time.IsStepFrame() && time.IsSimulating(), "the next frame is the step");
        Check(time.GetFrameTime().fixedStepCount == 1, "a step is exactly one fixed step");
        Check(Near(time.GetFrameTime().deltaTime, Sixtieth), "whatever the raw delta, the step moves one fixed delta");
        Check(time.GetFrameTime().paused, "the game is still paused during the step");
        time.BeginFrame(Sixtieth);
        Check(false == time.IsStepFrame() && time.GetFrameTime().fixedStepCount == 0, "the frame after the step is paused again");

        time.RequestStep();
        time.SetPaused(false);
        time.SetPaused(true);
        time.BeginFrame(Sixtieth);
        Check(false == time.IsStepFrame(), "resuming must drop a step that was waiting");
    }

    void TestRejectsBadInput()
    {
        JBro::System::TimeSystem time;
        Check(false == time.BeginFrame(-0.1f), "a negative delta is refused");
        Check(false == time.BeginFrame(std::numeric_limits<float>::infinity()), "an infinite delta is refused");
        Check(time.GetFrameTime().frameCount == 0, "a refused frame is not counted");

        JBro::TimeSettings settings;
        settings.fixedDeltaTime = 0.0f;
        Check(false == time.Configure(settings), "a zero step is refused");
        settings.fixedDeltaTime = 0.02f;
        settings.maxFixedSteps = 0;
        Check(false == time.Configure(settings), "zero steps per frame is refused");
        settings.maxFixedSteps = 8;
        settings.maxDeltaTime = 0.0f;
        Check(false == time.Configure(settings), "a zero ceiling is refused");
        Check(Near(time.GetSettings().fixedDeltaTime, Sixtieth), "refused settings leave the old ones");
        settings.maxDeltaTime = 1.0f;
        Check(time.Configure(settings), "valid settings are taken");
        time.BeginFrame(0.02f);
        Check(time.GetFrameTime().fixedStepCount == 1 && Near(time.GetFrameTime().fixedDeltaTime, 0.02), "the new step length is used");
    }

    void TestResetGameTime()
    {
        JBro::System::TimeSystem time;
        time.SetTimeScale(2.0f);
        time.BeginFrame(0.1f);
        time.BeginFixedStep();
        time.EndFixedSteps();
        time.ResetGameTime();
        const JBro::FrameTime& frame = time.GetFrameTime();
        Check(frame.time == 0.0 && frame.fixedTime == 0.0, "the game clock goes back to zero");
        Check(frame.timeScale == 1.0f, "the scale a game set does not survive the next play");
        Check(frame.frameCount == 1, "the frame count is the engine's and stays");
        time.BeginFrame(0.01f);
        Check(time.GetFrameTime().fixedStepCount == 0, "the accumulator was emptied too");
    }

    // ── 서비스 ───────────────────────────────────────────────────────────

    void TestServiceReadsFixedStepValuesInsideSteps()
    {
        JBro::System::TimeSystem time;
        const BoundSystems bound(&time, nullptr);
        const JBro::Service::TimeService& service = JBro::GetServiceContext().Time;
        time.BeginFrame(0.04f);
        Check(Near(service.DeltaTime(), 0.04), "outside a step the service gives the frame delta");
        Check(time.GetFrameTime().fixedStepCount == 2, "0.04 s is two steps");
        time.BeginFixedStep();
        Check(service.IsInFixedStep(), "the service knows it is inside a step");
        Check(Near(service.DeltaTime(), Sixtieth), "inside a step the delta is the fixed delta");
        Check(Near(service.Time(), Sixtieth), "and the time is the step's fixed time");
        time.BeginFixedStep();
        Check(Near(service.Time(), 2.0 * Sixtieth), "each step moves the fixed time");
        time.EndFixedSteps();
        Check(Near(service.DeltaTime(), 0.04) && Near(service.Time(), 0.04), "after the steps the frame values come back");
        Check(Near(service.UnscaledDeltaTime(), 0.04) && service.FrameCount() == 1, "the other values are the frame's");
        Check(service.SetTimeScale(3.0f) && service.TimeScale() == 3.0f, "a script may change the scale");
        Check(false == service.SetTimeScale(-2.0f), "but not to a bad one");
    }

    void TestUnboundServicesAreHarmless()
    {
        JBro::BindSystemContext({});
        const JBro::ServiceContext& services = JBro::GetServiceContext();
        Check(services.Time.DeltaTime() == 0.0f && services.Time.TimeScale() == 1.0f, "an unbound clock is a stopped clock");
        Check(false == services.Time.SetTimeScale(2.0f), "and cannot be changed");
        services.Random.SetSeed(42u);
        JBro::RandomStream reference(42u);
        Check(services.Random.UInt32() == reference.NextUInt32(), "an unbound random service draws from its own seeded stream");
    }

    // ── 프레임워크 ───────────────────────────────────────────────────────

    // 훅은 인자 없이 서비스에서 시간을 읽는다(D-231).
    class ClockProbe final : public JBro::GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Tests::ClockProbe";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnUpdate() override
        {
            const JBro::Service::TimeService& time = JBro::GetServiceContext().Time;
            ++updates;
            updateDelta = time.DeltaTime();
            updateInFixedStep = time.IsInFixedStep();
        }

        void OnFixedUpdate() override
        {
            const JBro::Service::TimeService& time = JBro::GetServiceContext().Time;
            ++fixedUpdates;
            fixedDelta = time.DeltaTime();
            fixedInFixedStep = time.IsInFixedStep();
            fixedTime = time.Time();
        }

        void Reset()
        {
            updates = 0;
            fixedUpdates = 0;
            updateDelta = -1.0f;
            fixedDelta = -1.0f;
        }

        int updates = 0;
        int fixedUpdates = 0;
        float updateDelta = -1.0f;
        float fixedDelta = -1.0f;
        bool updateInFixedStep = true;
        bool fixedInFixedStep = false;
        double fixedTime = -1.0;
    };

    void TestScriptsReadTheClockThroughTheService()
    {
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        Check(false == framework.Initialize(context), "a framework must refuse to start without the host clock");
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework starts with the clock");
        JBro::Canvas* canvas = framework.GetCanvas();
        ClockProbe* probe = canvas->AttachComponent<ClockProbe>(canvas->CreateObject("clock"));
        Check(probe != nullptr, "the probe script must attach");

        // 첫 프레임은 스크립트가 시작하는 프레임이다. 고정 스텝은 시작한 스크립트만 받으므로 한 프레임을 먼저 돈다.
        JBro::Testing::Tick(framework, Sixtieth);
        probe->Reset();
        JBro::Testing::Tick(framework, 0.04f);
        Check(probe->updates == 1 && Near(probe->updateDelta, 0.04), "OnUpdate reads the frame delta from the service");
        Check(false == probe->updateInFixedStep, "and knows it is outside a fixed step");
        Check(probe->fixedUpdates == 2, "0.04 s after an exact step is two fixed steps");
        Check(Near(probe->fixedDelta, Sixtieth) && probe->fixedInFixedStep, "OnFixedUpdate reads the fixed delta, inside a step");
        Check(Near(probe->fixedTime, 3.0 * Sixtieth), "and the fixed time of its own step");

        Check(JBro::GetServiceContext().Time.SetTimeScale(0.5f), "a script may slow the game down");
        probe->Reset();
        JBro::Testing::Tick(framework, 0.04f);
        Check(Near(probe->updateDelta, 0.02), "half speed halves the delta a script reads");

        // 멈춤: 호스트는 프레임워크와 시계를 함께 세운다(`EngineInstance::SetSimulationEnabled`).
        framework.SetSimulationEnabled(false);
        JBro::Testing::SharedClock().SetPaused(true);
        probe->Reset();
        JBro::Testing::Tick(framework, 0.04f);
        Check(probe->updates == 0 && probe->fixedUpdates == 0, "a paused game runs no hooks");

        JBro::Testing::SharedClock().RequestStep();
        JBro::Testing::Tick(framework, 0.5f);
        Check(probe->updates == 1 && probe->fixedUpdates == 1, "a single-frame step runs one fixed step and one update");
        Check(Near(probe->fixedDelta, Sixtieth) && Near(probe->updateDelta, Sixtieth), "and both read one fixed delta");
        probe->Reset();
        JBro::Testing::Tick(framework, 0.04f);
        Check(probe->updates == 0 && probe->fixedUpdates == 0, "after the step the scripts are stopped again");

        framework.SetSimulationEnabled(true);
        JBro::Testing::SharedClock().SetPaused(false);
        probe->Reset();
        JBro::Testing::Tick(framework, 0.04f);
        Check(probe->updates == 1, "resuming runs the hooks again");
        framework.Shutdown();
    }

    // 3D 도 멈추면 고정 스텝을 돌리지 않는다. 전에는 3D 만 제 누산기로 멈춘 동안에도 스텝을 돌렸다(time-plan T4).
    void TestThreeDimensionalFrameworkHonoursPause()
    {
        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the 3D framework starts with the clock");
        framework.SetSimulationEnabled(false);
        const double before = JBro::Testing::SharedClock().GetFrameTime().fixedTime;
        JBro::Testing::Tick(framework, 0.1f);
        Check(JBro::Testing::SharedClock().GetFrameTime().fixedTime == before, "a stopped 3D game must not run fixed steps");
        framework.SetSimulationEnabled(true);
        JBro::Testing::Tick(framework, 0.1f);
        Check(JBro::Testing::SharedClock().GetFrameTime().fixedTime > before, "a running one must");
        framework.Shutdown();
    }

    void TestRandomServiceUsesTheEngineStream()
    {
        const bool echo = JBro::Log::GetEchoToConsole();
        JBro::Log::SetEchoToConsole(false);
        JBro::System::RandomSystem random;
        random.Reseed(0);
        Check(random.GetSeed() != 0, "a zero seed asks for a fresh one, never zero itself");
        const std::uint64_t logged = JBro::Log::GetCount();
        random.Reseed(1234u);
        JBro::Log::SetEchoToConsole(echo);
        Check(random.GetSeed() == 1234u, "a configured seed is used as it is");
        Check(logged > 0, "the seed must be written to the log so the run can be replayed");

        const BoundSystems bound(nullptr, &random);
        const JBro::Service::RandomService& service = JBro::GetServiceContext().Random;
        service.SetSeed(42u);
        JBro::RandomStream reference(42u);
        for (int i = 0; i < 8; ++i)
        {
            Check(service.Range(0, 1000) == reference.Range(0, 1000), "the service must map the engine stream exactly like RandomStream");
        }
        Check(service.GetSeed() == 42u, "the service reports the engine seed");

        service.SetSeed(7u);
        const JBro::RandomStream first = service.MakeStream();
        service.SetSeed(7u);
        JBro::RandomStream second = service.MakeStream();
        JBro::RandomStream firstCopy = first;
        Check(firstCopy.NextUInt32() == second.NextUInt32(), "a stream made from a fixed seed is itself fixed");

        const JBro::RandomState saved = service.GetState();
        const float a = service.Value();
        service.SetState(saved);
        Check(service.Value() == a, "the engine state can be saved and restored");
    }
}

int RunTimeTests()
{
    try
    {
        TestStreamMatchesTheReferenceSequence();
        TestStreamsAreDeterministicAndSeparate();
        TestIntegerRangeCoversBothEnds();
        TestIntegerRangeIsUnbiased();
        TestBoundedRejectsTheBiasedZone();
        TestFloatRangeIsHalfOpen();
        TestChance();
        TestOneFrameIsOneFixedStep();
        TestFractionsCarryOver();
        TestLongFramesAreClampedAndDroppedStepsStopTheClock();
        TestTimeScale();
        TestPauseAndStep();
        TestRejectsBadInput();
        TestResetGameTime();
        TestServiceReadsFixedStepValuesInsideSteps();
        TestUnboundServicesAreHarmless();
        TestRandomServiceUsesTheEngineStream();
        TestScriptsReadTheClockThroughTheService();
        TestThreeDimensionalFrameworkHonoursPause();
    }
    catch (const std::exception&)
    {
        JBro::BindSystemContext({});
        return 1;
    }
    std::cout << "Time and random tests passed." << std::endl;
    return 0;
}
