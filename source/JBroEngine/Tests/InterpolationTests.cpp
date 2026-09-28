#include <JBro/Types/Easing.h>
#include <JBro/Types/SpringDamper.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    bool Near(float left, float right, float tolerance = 0.0005f)
    {
        return std::fabs(left - right) <= tolerance;
    }

    const char* NameOf(JBro::EaseKind kind)
    {
        switch (kind)
        {
        case JBro::EaseKind::Linear:       return "Linear";
        case JBro::EaseKind::SineIn:       return "SineIn";
        case JBro::EaseKind::SineOut:      return "SineOut";
        case JBro::EaseKind::SineInOut:    return "SineInOut";
        case JBro::EaseKind::QuadIn:       return "QuadIn";
        case JBro::EaseKind::QuadOut:      return "QuadOut";
        case JBro::EaseKind::QuadInOut:    return "QuadInOut";
        case JBro::EaseKind::CubicIn:      return "CubicIn";
        case JBro::EaseKind::CubicOut:     return "CubicOut";
        case JBro::EaseKind::CubicInOut:   return "CubicInOut";
        case JBro::EaseKind::ExpoIn:       return "ExpoIn";
        case JBro::EaseKind::ExpoOut:      return "ExpoOut";
        case JBro::EaseKind::ExpoInOut:    return "ExpoInOut";
        case JBro::EaseKind::BackIn:       return "BackIn";
        case JBro::EaseKind::BackOut:      return "BackOut";
        case JBro::EaseKind::BackInOut:    return "BackInOut";
        case JBro::EaseKind::ElasticIn:    return "ElasticIn";
        case JBro::EaseKind::ElasticOut:   return "ElasticOut";
        case JBro::EaseKind::ElasticInOut: return "ElasticInOut";
        case JBro::EaseKind::BounceIn:     return "BounceIn";
        case JBro::EaseKind::BounceOut:    return "BounceOut";
        case JBro::EaseKind::BounceInOut:  return "BounceInOut";
        case JBro::EaseKind::Count:        break;
        }
        return "?";
    }

    // **모든 모양이 0 에서 0, 1 에서 1 로 끝난다.** 여기가 어긋나면 애니메이션이 끝난 뒤 값이
    // 목표에 닿지 않고 남는다 - 화면에서 1 픽셀씩 밀린 채 멈추는 종류의 결함이다.
    void TestEveryKindStartsAtZeroAndEndsAtOne()
    {
        for (int index = 0; index < static_cast<int>(JBro::EaseKind::Count); ++index)
        {
            const JBro::EaseKind kind = static_cast<JBro::EaseKind>(index);
            const float atZero = JBro::Ease(kind, 0.0f);
            const float atOne = JBro::Ease(kind, 1.0f);
            if (false == Near(atZero, 0.0f) || false == Near(atOne, 1.0f))
            {
                std::cout << "  " << NameOf(kind) << ": 0 -> " << atZero << ", 1 -> " << atOne << '\n';
            }
            Check(Near(atZero, 0.0f), "every easing kind starts at zero");
            Check(Near(atOne, 1.0f), "every easing kind ends at one");
        }
    }

    // **`Linear` 는 진행도를 그대로 돌려준다.** 기본값이자 모르는 값이 왔을 때의 대비책이라,
    // 여기에 무엇이라도 섞이면 "곡선 없음" 을 고를 수 없게 된다.
    void TestLinearIsIdentity()
    {
        for (int step = 0; step <= 20; ++step)
        {
            const float t = static_cast<float>(step) / 20.0f;
            Check(Near(JBro::Ease(JBro::EaseKind::Linear, t), t), "linear returns the progress unchanged");
        }
    }

    // **진행도는 0..1 로 자른다.** 경과 시간을 길이로 나누면 마지막 프레임에 1 을 넘기 마련이고,
    // 자르지 않으면 `Expo` 가 크게 튄다.
    void TestProgressIsClamped()
    {
        for (int index = 0; index < static_cast<int>(JBro::EaseKind::Count); ++index)
        {
            const JBro::EaseKind kind = static_cast<JBro::EaseKind>(index);
            Check(Near(JBro::Ease(kind, -3.0f), JBro::Ease(kind, 0.0f)), "progress below zero is clamped to zero");
            Check(Near(JBro::Ease(kind, 7.5f), JBro::Ease(kind, 1.0f)), "progress above one is clamped to one");
        }
    }

    // **`Out` 은 `In` 을 뒤집은 것이다.** 한쪽만 고치고 다른 쪽을 잊는 일이 흔해서 관계로 묶어 둔다.
    // `Elastic` 은 표준 공식이 정확한 거울이 아니므로 뺀다.
    void TestOutIsTheMirrorOfIn()
    {
        const JBro::EaseKind pairs[][2] = {
            { JBro::EaseKind::SineIn,   JBro::EaseKind::SineOut },
            { JBro::EaseKind::QuadIn,   JBro::EaseKind::QuadOut },
            { JBro::EaseKind::CubicIn,  JBro::EaseKind::CubicOut },
            { JBro::EaseKind::ExpoIn,   JBro::EaseKind::ExpoOut },
            { JBro::EaseKind::BackIn,   JBro::EaseKind::BackOut },
            { JBro::EaseKind::BounceIn, JBro::EaseKind::BounceOut },
        };

        for (const auto& pair : pairs)
        {
            for (int step = 0; step <= 20; ++step)
            {
                const float t = static_cast<float>(step) / 20.0f;
                const float out = JBro::Ease(pair[1], t);
                const float mirrored = 1.0f - JBro::Ease(pair[0], 1.0f - t);
                Check(Near(out, mirrored), "an out curve is its in curve mirrored");
            }
        }
    }

    // **`InOut` 은 한가운데에서 절반에 있다.** 양끝이 느린 모양이므로 가운데가 대칭점이다.
    void TestInOutIsHalfwayAtTheMiddle()
    {
        const JBro::EaseKind kinds[] = {
            JBro::EaseKind::SineInOut, JBro::EaseKind::QuadInOut, JBro::EaseKind::CubicInOut,
            JBro::EaseKind::ExpoInOut, JBro::EaseKind::BackInOut, JBro::EaseKind::BounceInOut,
        };
        for (const JBro::EaseKind kind : kinds)
        {
            Check(Near(JBro::Ease(kind, 0.5f), 0.5f), "an in-out curve passes through the middle");
        }
    }

    // **되돌아가지 않는다.** `Back`·`Elastic`·`Bounce` 는 일부러 넘나들거나 튀므로 뺀다 -
    // 나머지가 도중에 뒷걸음질하면 화면에서 값이 떨리는 것으로 보인다.
    void TestMonotonicKindsNeverGoBackwards()
    {
        const JBro::EaseKind kinds[] = {
            JBro::EaseKind::Linear,
            JBro::EaseKind::SineIn, JBro::EaseKind::SineOut, JBro::EaseKind::SineInOut,
            JBro::EaseKind::QuadIn, JBro::EaseKind::QuadOut, JBro::EaseKind::QuadInOut,
            JBro::EaseKind::CubicIn, JBro::EaseKind::CubicOut, JBro::EaseKind::CubicInOut,
            JBro::EaseKind::ExpoIn, JBro::EaseKind::ExpoOut, JBro::EaseKind::ExpoInOut,
        };
        for (const JBro::EaseKind kind : kinds)
        {
            float previous = JBro::Ease(kind, 0.0f);
            for (int step = 1; step <= 200; ++step)
            {
                const float value = JBro::Ease(kind, static_cast<float>(step) / 200.0f);
                if (value < previous - 0.0001f)
                {
                    std::cout << "  " << NameOf(kind) << " went from " << previous << " to " << value << '\n';
                }
                Check(value >= previous - 0.0001f, "a monotonic curve never goes backwards");
                previous = value;
            }
        }
    }

    // **`Back` 과 `Elastic` 은 0..1 을 실제로 벗어난다.** 벗어나는 것이 그 모양의 전부이므로,
    // 누군가 결과를 잘라 버리면 이 시험이 잡는다.
    void TestBackAndElasticLeaveTheUnitRange()
    {
        bool backWentBelowZero = false;
        bool elasticWentAboveOne = false;
        for (int step = 0; step <= 200; ++step)
        {
            const float t = static_cast<float>(step) / 200.0f;
            if (JBro::Ease(JBro::EaseKind::BackIn, t) < -0.001f)
            {
                backWentBelowZero = true;
            }
            if (JBro::Ease(JBro::EaseKind::ElasticOut, t) > 1.001f)
            {
                elasticWentAboveOne = true;
            }
        }
        Check(backWentBelowZero, "back pulls below zero before it moves");
        Check(elasticWentAboveOne, "elastic overshoots past one");
    }

    // **`Bounce` 는 실제로 튄다.** 공이 바닥에 닿았다가 다시 오르듯 값이 한 번 내려간다.
    // 이것이 없으면 `Bounce` 는 그냥 또 하나의 완만한 곡선일 뿐이다.
    void TestBounceActuallyBounces()
    {
        int descents = 0;
        float previous = JBro::Ease(JBro::EaseKind::BounceOut, 0.0f);
        for (int step = 1; step <= 400; ++step)
        {
            const float value = JBro::Ease(JBro::EaseKind::BounceOut, static_cast<float>(step) / 400.0f);
            if (value < previous - 0.001f)
            {
                ++descents;
            }
            previous = value;
        }
        Check(descents > 0, "bounce falls back at least once on its way up");
        Check(Near(JBro::Ease(JBro::EaseKind::BounceOut, 1.0f), 1.0f), "and still lands exactly on one");
    }

    // **모르는 값은 `Linear` 로 본다.** 파일에서 읽은 열거형이 범위를 벗어날 수 있고,
    // 그때 멈추거나 0 을 돌려주면 화면이 굳는다.
    void TestUnknownKindFallsBackToLinear()
    {
        const JBro::EaseKind unknown = static_cast<JBro::EaseKind>(200);
        for (int step = 0; step <= 10; ++step)
        {
            const float t = static_cast<float>(step) / 10.0f;
            Check(Near(JBro::Ease(unknown, t), t), "an unknown kind behaves as linear");
        }
    }

    void TestEaseBetweenMixesTheTwoEnds()
    {
        Check(Near(JBro::EaseBetween(JBro::EaseKind::Linear, 10.0f, 20.0f, 0.0f), 10.0f), "it starts at from");
        Check(Near(JBro::EaseBetween(JBro::EaseKind::Linear, 10.0f, 20.0f, 1.0f), 20.0f), "and ends at to");
        Check(Near(JBro::EaseBetween(JBro::EaseKind::Linear, 10.0f, 20.0f, 0.5f), 15.0f), "and halfway in between");
        Check(Near(JBro::EaseBetween(JBro::EaseKind::CubicOut, 4.0f, -4.0f, 1.0f), -4.0f),
            "a descending range also lands on its end");
    }

    // ── SpringDamper ──────────────────────────────────────────────────────────────────────────

    // **목표에 닿는다.** 따라가기만 하고 닿지 않으면 값이 영원히 조금씩 남는다.
    void TestItReachesTheTarget()
    {
        JBro::SpringDamper damper;
        float value = 0.0f;
        for (int frame = 0; frame < 600; ++frame)
        {
            value = damper.Update(value, 10.0f, 0.2f, 1.0f / 60.0f);
        }
        Check(Near(value, 10.0f, 0.01f), "the value reaches its target");
        Check(Near(damper.velocity, 0.0f, 0.01f), "and settles with no speed left");
    }

    // **지나치지 않는다.** 임계 감쇠라고 적었으므로 지켜야 한다 - 지나치면 카메라가 목표를
    // 넘어갔다 돌아오는 것으로 보인다.
    void TestItNeverOvershoots()
    {
        JBro::SpringDamper damper;
        float value = 0.0f;
        for (int frame = 0; frame < 600; ++frame)
        {
            value = damper.Update(value, 10.0f, 0.2f, 1.0f / 60.0f);
            Check(value <= 10.0f + 0.0001f, "the value never passes its target from below");
        }

        damper.Reset();
        value = 10.0f;
        for (int frame = 0; frame < 600; ++frame)
        {
            value = damper.Update(value, -5.0f, 0.2f, 1.0f / 60.0f);
            Check(value >= -5.0f - 0.0001f, "nor from above");
        }
    }

    // **프레임 시간이 흔들려도 같은 자리에 온다.** 이것이 무너지면 프레임률이 높은 기계에서
    // 카메라가 더 빨리 따라붙는다.
    void TestItIsIndependentOfTheFrameRate()
    {
        JBro::SpringDamper coarse;
        float coarseValue = 0.0f;
        for (int frame = 0; frame < 30; ++frame)
        {
            coarseValue = coarse.Update(coarseValue, 10.0f, 0.5f, 1.0f / 30.0f);
        }

        JBro::SpringDamper fine;
        float fineValue = 0.0f;
        for (int frame = 0; frame < 120; ++frame)
        {
            fineValue = fine.Update(fineValue, 10.0f, 0.5f, 1.0f / 120.0f);
        }

        Check(Near(coarseValue, fineValue, 0.05f), "a coarse and a fine frame step land in the same place");
    }

    // **시간이 흐르지 않으면 아무 일도 없다.** 멈춤이나 한 프레임 진행에서 델타가 0 으로 온다.
    void TestZeroDeltaChangesNothing()
    {
        JBro::SpringDamper damper;
        damper.velocity = 3.0f;
        const float result = damper.Update(2.0f, 10.0f, 0.2f, 0.0f);
        Check(Near(result, 2.0f), "a zero delta leaves the value alone");
        Check(Near(damper.velocity, 3.0f), "and leaves the speed alone");
        Check(Near(damper.Update(2.0f, 10.0f, 0.2f, -1.0f), 2.0f), "a negative delta does nothing either");

        // **이미 목표에 앉아 있는데 델타가 0 인 경우가 가장 위험하다.** 지나침을 막는 자리가
        // 남은 거리를 델타로 나누므로, 막지 않으면 0 을 0 으로 나눠 속도가 수가 아니게 된다.
        JBro::SpringDamper settled;
        const float stayed = settled.Update(5.0f, 5.0f, 0.2f, 0.0f);
        Check(Near(stayed, 5.0f), "sitting on the target with no time passing keeps the value");
        Check(settled.velocity == settled.velocity, "and the speed is still a number, not a NaN");
        Check(std::isfinite(settled.velocity), "and it is finite");

        // **시간이 안 흘렀으면 매끄러움이 0 이어도 움직이지 않는다.** 매끄러움 0 은 "곧바로 둔다" 는
        // 뜻이지만, 그것도 한 프레임이 흘러야 하는 일이다. 멈춰 둔 상태에서 값이 튀면 안 된다.
        JBro::SpringDamper frozen;
        Check(Near(frozen.Update(2.0f, 10.0f, 0.0f, 0.0f), 2.0f),
            "no time passing beats a zero smooth time, so the value stays put");
    }

    // **`smoothTime` 은 실제로 걸리는 시간을 뜻한다.** 이것을 재지 않으면 따라가는 세기를 아무렇게나
    // 바꿔도 다른 시험이 모두 통과한다 - 목표에는 어차피 닿고, 지나치지도 않기 때문이다.
    // 임계 감쇠에서 `smoothTime` 만큼 지나면 남은 거리의 3/e² 쯤이 남는다(약 59 % 진행).
    void TestSmoothTimeMeansHowLongItTakes()
    {
        JBro::SpringDamper damper;
        float value = 0.0f;
        const float smoothTime = 0.5f;
        const int framesForOneSmoothTime = 30;      // 60 분의 1 초로 0.5 초
        for (int frame = 0; frame < framesForOneSmoothTime; ++frame)
        {
            value = damper.Update(value, 10.0f, smoothTime, 1.0f / 60.0f);
        }
        Check(value > 5.0f, "after one smooth time it is well past halfway");
        Check(value < 7.0f, "but not nearly all the way there");

        // 절반의 시간을 주면 절반보다 덜 간다.
        JBro::SpringDamper quicker;
        float quickValue = 0.0f;
        for (int frame = 0; frame < framesForOneSmoothTime; ++frame)
        {
            quickValue = quicker.Update(quickValue, 10.0f, smoothTime / 2.0f, 1.0f / 60.0f);
        }
        Check(quickValue > value, "a shorter smooth time gets there sooner");
    }

    // **따라갈 시간을 주지 않으면 곧바로 둔다.** 속도까지 지워야 다음 프레임에 밀리지 않는다.
    void TestZeroSmoothTimeSnapsAndClearsSpeed()
    {
        JBro::SpringDamper damper;
        damper.velocity = 99.0f;
        const float result = damper.Update(0.0f, 7.0f, 0.0f, 1.0f / 60.0f);
        Check(Near(result, 7.0f), "a zero smooth time puts the value on the target");
        Check(Near(damper.velocity, 0.0f), "and clears the speed so the next frame does not drift");
    }

    // **속도 제한이 실제로 느리게 만든다.** 제한이 없을 때보다 덜 가야 한다.
    void TestMaxSpeedSlowsItDown()
    {
        JBro::SpringDamper unlimited;
        JBro::SpringDamper limited;
        float unlimitedValue = 0.0f;
        float limitedValue = 0.0f;
        for (int frame = 0; frame < 10; ++frame)
        {
            unlimitedValue = unlimited.Update(unlimitedValue, 100.0f, 0.3f, 1.0f / 60.0f);
            limitedValue = limited.Update(limitedValue, 100.0f, 0.3f, 1.0f / 60.0f, 5.0f);
        }
        Check(limitedValue < unlimitedValue, "a speed limit holds the value back");
        Check(limitedValue > 0.0f, "but it still moves");
    }

    // **성분마다 따로 돈다.** 2D·3D 판이 스칼라를 축마다 부른 것과 같아야, 하나를 고쳤을 때
    // 나머지가 따라온다.
    void TestVectorFormsMatchTheScalarPerAxis()
    {
        JBro::SpringDamper2D vectorDamper;
        JBro::SpringDamper axisX;
        JBro::SpringDamper axisY;

        JBro::Vector2 current{0.0f, 0.0f};
        float scalarX = 0.0f;
        float scalarY = 0.0f;
        const JBro::Vector2 target{10.0f, -4.0f};

        for (int frame = 0; frame < 60; ++frame)
        {
            current = vectorDamper.Update(current, target, 0.25f, 1.0f / 60.0f);
            scalarX = axisX.Update(scalarX, target.x, 0.25f, 1.0f / 60.0f);
            scalarY = axisY.Update(scalarY, target.y, 0.25f, 1.0f / 60.0f);
        }
        Check(Near(current.x, scalarX), "the 2D form matches the scalar on x");
        Check(Near(current.y, scalarY), "and on y");

        JBro::SpringDamper3D spatial;
        JBro::Vector3 point{0.0f, 0.0f, 0.0f};
        JBro::Vector3 goal;
        goal.x = 1.0f;
        goal.y = 2.0f;
        goal.z = 3.0f;
        for (int frame = 0; frame < 300; ++frame)
        {
            point = spatial.Update(point, goal, 0.2f, 1.0f / 60.0f);
        }
        Check(Near(point.x, 1.0f, 0.01f) && Near(point.y, 2.0f, 0.01f) && Near(point.z, 3.0f, 0.01f),
            "the 3D form reaches its target on every axis");

        // **아직 달리는 중에 지운다.** 수렴한 뒤에 지우면 속도가 이미 0 이라, 지웠는지 안 지웠는지를
        // 가릴 수 없다 - 축 하나를 빠뜨려도 시험이 통과해 버린다.
        JBro::SpringDamper3D running;
        JBro::Vector3 moving{0.0f, 0.0f, 0.0f};
        for (int frame = 0; frame < 5; ++frame)
        {
            moving = running.Update(moving, goal, 0.5f, 1.0f / 60.0f);
        }
        Check(std::fabs(running.x.velocity) > 0.01f, "the x axis really is moving before the reset");
        Check(std::fabs(running.y.velocity) > 0.01f, "so is y");
        Check(std::fabs(running.z.velocity) > 0.01f, "so is z");

        running.Reset();
        Check(Near(running.x.velocity, 0.0f), "resetting clears x");
        Check(Near(running.y.velocity, 0.0f), "and y");
        Check(Near(running.z.velocity, 0.0f), "and z, not just the first axes");
    }

    // **경계를 넘을 수 있는 값이다.** 컴포넌트 칸에 두려면 POD 여야 한다.
    void TestTheseArePlainData()
    {
        static_assert(std::is_trivially_copyable_v<JBro::SpringDamper>, "a damper must be plain data");
        static_assert(std::is_standard_layout_v<JBro::SpringDamper>, "and keep a predictable layout");
        static_assert(std::is_trivially_copyable_v<JBro::SpringDamper2D>, "so must the 2D form");
        static_assert(std::is_trivially_copyable_v<JBro::SpringDamper3D>, "and the 3D form");
        static_assert(sizeof(JBro::SpringDamper) == sizeof(float), "a damper is one float of state");
        static_assert(sizeof(JBro::EaseKind) == 1, "an ease kind fits in a byte for serialisation");

        JBro::SpringDamper damper;
        Check(Near(damper.velocity, 0.0f), "a fresh damper has no speed");
    }
}

int RunInterpolationTests()
{
    try
    {
        TestEveryKindStartsAtZeroAndEndsAtOne();
        TestLinearIsIdentity();
        TestProgressIsClamped();
        TestOutIsTheMirrorOfIn();
        TestInOutIsHalfwayAtTheMiddle();
        TestMonotonicKindsNeverGoBackwards();
        TestBackAndElasticLeaveTheUnitRange();
        TestBounceActuallyBounces();
        TestUnknownKindFallsBackToLinear();
        TestEaseBetweenMixesTheTwoEnds();

        TestItReachesTheTarget();
        TestItNeverOvershoots();
        TestItIsIndependentOfTheFrameRate();
        TestZeroDeltaChangesNothing();
        TestZeroSmoothTimeSnapsAndClearsSpeed();
        TestSmoothTimeMeansHowLongItTakes();
        TestMaxSpeedSlowsItDown();
        TestVectorFormsMatchTheScalarPerAxis();
        TestTheseArePlainData();
    }
    catch (const std::exception&)
    {
        return 1;
    }

    std::cout << "interpolation tests passed\n";
    return 0;
}
