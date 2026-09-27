#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 난수 흐름 하나의 전부다(D-242). 세이브에 적거나 되감을 때 이것을 그대로 옮긴다.
    struct RandomState
    {
        std::uint64_t state = 0;
        // 홀수여야 한다. `RandomStream::SetState` 가 짝수를 홀수로 고친다.
        std::uint64_t increment = 1;
    };

    // 구간 매핑이다. `RandomStream` 과 엔진 흐름을 뽑는 `Service::RandomService` 가 **같은 식**을 쓰게 여기 한 번만 둔다 -
    // 표준 분포(`std::uniform_int_distribution` 따위)는 구현마다 다른 수를 내서, 같은 씨앗이 컴파일러마다 다른 게임을 만든다(time-plan R1).
    // `next` 는 인자 없이 32 비트 난수를 주는 것이다.
    namespace RandomMapping
    {
        // [0, range) 의 치우침 없는 정수다(Lemire 의 곱셈 거절). range 가 0 이면 32 비트 전체다.
        template <typename TNext>
        std::uint32_t Bounded(TNext& next, std::uint32_t range)
        {
            std::uint32_t bits = next();
            if (range == 0)
            {
                return bits;
            }
            std::uint64_t product = static_cast<std::uint64_t>(bits) * range;
            std::uint32_t low = static_cast<std::uint32_t>(product);
            if (low < range)
            {
                const std::uint32_t threshold = (0u - range) % range;
                while (low < threshold)
                {
                    bits = next();
                    product = static_cast<std::uint64_t>(bits) * range;
                    low = static_cast<std::uint32_t>(product);
                }
            }
            return static_cast<std::uint32_t>(product >> 32);
        }

        // [min, max] 의 정수다. min 이 max 보다 크면 둘을 바꾼다.
        template <typename TNext>
        std::int32_t RangeInt(TNext& next, std::int32_t min, std::int32_t max)
        {
            if (min > max)
            {
                const std::int32_t swapped = min;
                min = max;
                max = swapped;
            }
            // 폭은 2^32 까지 간다. 그때 32 비트로 옮기면 0 이 되고 `Bounded` 는 그것을 전체로 읽는다.
            const std::uint64_t width = static_cast<std::uint64_t>(static_cast<std::int64_t>(max) - min) + 1;
            const std::uint32_t offset = Bounded(next, static_cast<std::uint32_t>(width));
            return static_cast<std::int32_t>(static_cast<std::int64_t>(min) + offset);
        }

        // [0, 1) 이다. 위 24 비트를 쓴다 - float 의 가수가 그만큼이라 모든 값이 같은 간격이다.
        template <typename TNext>
        float Value(TNext& next)
        {
            return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
        }

        // [min, max) 이다. min 이 max 보다 크면 둘을 바꾸고, 같으면 그 값이다.
        template <typename TNext>
        float RangeFloat(TNext& next, float min, float max)
        {
            if (min > max)
            {
                const float swapped = min;
                min = max;
                max = swapped;
            }
            const float value = min + (max - min) * Value(next);
            // 반올림이 max 에 닿으면 그 바로 아래로 둔다 - 끝을 열어 둔다는 약속을 지킨다.
            if (value >= max && max > min)
            {
                return std::nextafter(max, min);
            }
            return value;
        }

        // 참일 확률이 p 다. 0 이하면 언제나 거짓, 1 이상이면 언제나 참이다. 한 번 뽑는다.
        template <typename TNext>
        bool Chance(TNext& next, float probability)
        {
            return Value(next) < probability;
        }
    }

    // 씨앗으로 정해지는 난수 흐름이다(D-242). PCG32(XSH-RR, O'Neill 2014)이고 16 바이트 값이다.
    //
    // **같은 씨앗과 흐름 번호면 컴파일러·플랫폼과 무관하게 같은 수열이다.** 게임이 제 흐름을 들고 싶을 때(적 AI·지형 생성·파티클) 컴포넌트
    // 필드나 스크립트 멤버로 둔다 - 엔진 흐름(`Service::RandomService`)을 나눠 쓰면 새 효과 하나가 다른 것의 난수를 밀어낸다.
    // 잠그지 않는다. 워커는 제 흐름을 든다.
    class RandomStream
    {
    public:
        RandomStream()
        {
            Seed(0x853C49E6748FEA9Bull, 0xDA3E39CB94B95BDBull);
        }

        explicit RandomStream(std::uint64_t seed, std::uint64_t stream = 0)
        {
            Seed(seed, stream);
        }

        // PCG 의 `pcg32_srandom_r` 과 같다. 흐름 번호가 다르면 같은 씨앗이라도 겹치지 않는 수열이다.
        void Seed(std::uint64_t seed, std::uint64_t stream = 0)
        {
            m_state.state = 0;
            m_state.increment = (stream << 1u) | 1u;
            NextUInt32();
            m_state.state += seed;
            NextUInt32();
        }

        std::uint32_t NextUInt32()
        {
            const std::uint64_t previous = m_state.state;
            m_state.state = previous * 6364136223846793005ull + m_state.increment;
            const std::uint32_t shifted = static_cast<std::uint32_t>(((previous >> 18u) ^ previous) >> 27u);
            const std::uint32_t rotation = static_cast<std::uint32_t>(previous >> 59u);
            return (shifted >> rotation) | (shifted << ((0u - rotation) & 31u));
        }

        std::uint64_t NextUInt64()
        {
            const std::uint64_t high = NextUInt32();
            return (high << 32u) | NextUInt32();
        }

        // [min, max] 의 정수다.
        std::int32_t Range(std::int32_t min, std::int32_t max)
        {
            return RandomMapping::RangeInt(*this, min, max);
        }

        // [min, max) 의 실수다.
        float Range(float min, float max)
        {
            return RandomMapping::RangeFloat(*this, min, max);
        }

        // [0, 1) 이다.
        float Value()
        {
            return RandomMapping::Value(*this);
        }

        bool Chance(float probability)
        {
            return RandomMapping::Chance(*this, probability);
        }

        RandomState GetState() const
        {
            return m_state;
        }

        void SetState(const RandomState& state)
        {
            m_state.state = state.state;
            m_state.increment = state.increment | 1u;
        }

        // `RandomMapping` 이 부르는 자리다.
        std::uint32_t operator()()
        {
            return NextUInt32();
        }

    private:
        RandomState m_state;
    };

    static_assert(sizeof(RandomStream) == 16);
    static_assert(std::is_trivially_copyable_v<RandomStream>);
}
