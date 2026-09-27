#include <JBro/Runtime/RandomService.h>

#include <JBro/Runtime/SystemContext.h>

namespace JBro::Service
{
    namespace
    {
        // 묶인 난수 시스템이 없을 때 뽑는 이 모듈 사본의 흐름이다. 씨앗이 고정이라 시험이 같은 수를 본다.
        RandomStream& FallbackStream()
        {
            static RandomStream stream;
            return stream;
        }

        // `RandomMapping` 에 넘길 32 비트 원천이다. 엔진 흐름이 있으면 그것, 없으면 이 사본의 흐름이다.
        struct EngineBits
        {
            System::IRandomSystem* system = GetSystemContext().Random;

            std::uint32_t operator()()
            {
                return system != nullptr ? system->NextUInt32() : FallbackStream().NextUInt32();
            }
        };
    }

    std::uint32_t RandomService::UInt32() const
    {
        EngineBits bits;
        return bits();
    }

    float RandomService::Value() const
    {
        EngineBits bits;
        return RandomMapping::Value(bits);
    }

    std::int32_t RandomService::Range(std::int32_t min, std::int32_t max) const
    {
        EngineBits bits;
        return RandomMapping::RangeInt(bits, min, max);
    }

    float RandomService::Range(float min, float max) const
    {
        EngineBits bits;
        return RandomMapping::RangeFloat(bits, min, max);
    }

    bool RandomService::Chance(float probability) const
    {
        EngineBits bits;
        return RandomMapping::Chance(bits, probability);
    }

    std::uint64_t RandomService::GetSeed() const
    {
        const System::IRandomSystem* system = GetSystemContext().Random;
        return system != nullptr ? system->GetSeed() : 0;
    }

    void RandomService::SetSeed(std::uint64_t seed) const
    {
        System::IRandomSystem* system = GetSystemContext().Random;
        if (system != nullptr)
        {
            system->SetSeed(seed);
            return;
        }
        FallbackStream().Seed(seed);
    }

    RandomState RandomService::GetState() const
    {
        const System::IRandomSystem* system = GetSystemContext().Random;
        return system != nullptr ? system->GetState() : FallbackStream().GetState();
    }

    void RandomService::SetState(const RandomState& state) const
    {
        System::IRandomSystem* system = GetSystemContext().Random;
        if (system != nullptr)
        {
            system->SetState(state);
            return;
        }
        FallbackStream().SetState(state);
    }

    RandomStream RandomService::MakeStream() const
    {
        EngineBits bits;
        const std::uint64_t seedHigh = bits();
        const std::uint64_t seed = (seedHigh << 32) | bits();
        const std::uint64_t streamHigh = bits();
        const std::uint64_t stream = (streamHigh << 32) | bits();
        return RandomStream(seed, stream);
    }
}
