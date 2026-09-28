#include <JBro/Types/Easing.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

        // Penner 관행의 상수들이다. `Back` 이 얼마나 되돌아갔다 가는지, `Elastic` 이 몇 번 떠는지,
        // `Bounce` 가 몇 번 튀는지를 정한다. 널리 쓰이는 값이라 그대로 둔다.
        constexpr float BackC1 = 1.70158f;
        constexpr float BackC3 = BackC1 + 1.0f;
        constexpr float BackC2 = BackC1 * 1.525f;
        constexpr float ElasticC4 = (2.0f * Pi) / 3.0f;
        constexpr float ElasticC5 = (2.0f * Pi) / 4.5f;
        constexpr float BounceN1 = 7.5625f;
        constexpr float BounceD1 = 2.75f;

        float BounceOut(float t) noexcept
        {
            if (t < 1.0f / BounceD1)
            {
                return BounceN1 * t * t;
            }
            if (t < 2.0f / BounceD1)
            {
                const float shifted = t - 1.5f / BounceD1;
                return BounceN1 * shifted * shifted + 0.75f;
            }
            if (t < 2.5f / BounceD1)
            {
                const float shifted = t - 2.25f / BounceD1;
                return BounceN1 * shifted * shifted + 0.9375f;
            }
            const float shifted = t - 2.625f / BounceD1;
            return BounceN1 * shifted * shifted + 0.984375f;
        }
    }

    float Ease(EaseKind kind, float t) noexcept
    {
        // 부르는 쪽이 경과 시간을 길이로 나누다 보면 마지막 프레임에 1 을 넘는다. 여기서 한 번만 자른다.
        if (t <= 0.0f)
        {
            t = 0.0f;
        }
        else if (t >= 1.0f)
        {
            t = 1.0f;
        }

        switch (kind)
        {
        case EaseKind::Linear:
            return t;

        case EaseKind::SineIn:
            return 1.0f - std::cos((t * Pi) / 2.0f);
        case EaseKind::SineOut:
            return std::sin((t * Pi) / 2.0f);
        case EaseKind::SineInOut:
            return -(std::cos(Pi * t) - 1.0f) / 2.0f;

        case EaseKind::QuadIn:
            return t * t;
        case EaseKind::QuadOut:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case EaseKind::QuadInOut:
            if (t < 0.5f)
            {
                return 2.0f * t * t;
            }
            return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;

        case EaseKind::CubicIn:
            return t * t * t;
        case EaseKind::CubicOut:
            return 1.0f - std::pow(1.0f - t, 3.0f);
        case EaseKind::CubicInOut:
            if (t < 0.5f)
            {
                return 4.0f * t * t * t;
            }
            return 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;

        // 지수는 0 과 1 을 따로 짚는다. 공식만 쓰면 양 끝이 0 과 1 에 닿지 않는다.
        case EaseKind::ExpoIn:
            if (t <= 0.0f)
            {
                return 0.0f;
            }
            return std::pow(2.0f, 10.0f * t - 10.0f);
        case EaseKind::ExpoOut:
            if (t >= 1.0f)
            {
                return 1.0f;
            }
            return 1.0f - std::pow(2.0f, -10.0f * t);
        case EaseKind::ExpoInOut:
            if (t <= 0.0f)
            {
                return 0.0f;
            }
            if (t >= 1.0f)
            {
                return 1.0f;
            }
            if (t < 0.5f)
            {
                return std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f;
            }
            return (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;

        case EaseKind::BackIn:
            return BackC3 * t * t * t - BackC1 * t * t;
        case EaseKind::BackOut:
            return 1.0f + BackC3 * std::pow(t - 1.0f, 3.0f) + BackC1 * std::pow(t - 1.0f, 2.0f);
        case EaseKind::BackInOut:
            if (t < 0.5f)
            {
                return (std::pow(2.0f * t, 2.0f) * ((BackC2 + 1.0f) * 2.0f * t - BackC2)) / 2.0f;
            }
            return (std::pow(2.0f * t - 2.0f, 2.0f) * ((BackC2 + 1.0f) * (t * 2.0f - 2.0f) + BackC2) + 2.0f) / 2.0f;

        case EaseKind::ElasticIn:
            if (t <= 0.0f)
            {
                return 0.0f;
            }
            if (t >= 1.0f)
            {
                return 1.0f;
            }
            return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * ElasticC4);
        case EaseKind::ElasticOut:
            if (t <= 0.0f)
            {
                return 0.0f;
            }
            if (t >= 1.0f)
            {
                return 1.0f;
            }
            return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * ElasticC4) + 1.0f;
        case EaseKind::ElasticInOut:
            if (t <= 0.0f)
            {
                return 0.0f;
            }
            if (t >= 1.0f)
            {
                return 1.0f;
            }
            if (t < 0.5f)
            {
                return -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * ElasticC5)) / 2.0f;
            }
            return (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * ElasticC5)) / 2.0f + 1.0f;

        case EaseKind::BounceIn:
            return 1.0f - BounceOut(1.0f - t);
        case EaseKind::BounceOut:
            return BounceOut(t);
        case EaseKind::BounceInOut:
            if (t < 0.5f)
            {
                return (1.0f - BounceOut(1.0f - 2.0f * t)) / 2.0f;
            }
            return (1.0f + BounceOut(2.0f * t - 1.0f)) / 2.0f;

        case EaseKind::Count:
        default:
            // 파일에서 읽은 값이 범위를 벗어날 수 있다. 멈추는 것보다 곧게 가는 편이 낫다.
            return t;
        }
    }

    float EaseBetween(EaseKind kind, float from, float to, float t) noexcept
    {
        return from + (to - from) * Ease(kind, t);
    }
}
