#include <JBro/Runtime/RandomService.h>

#include <JBro/Runtime/SystemContext.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

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

            UInt32 operator()()
            {
                return system != nullptr ? system->NextUInt32() : FallbackStream().NextUInt32();
            }
        };
    }

    JBro::UInt32 RandomService::UInt32() const
    {
        EngineBits bits;
        return bits();
    }

    Float RandomService::Value() const
    {
        EngineBits bits;
        return RandomMapping::Value(bits);
    }

    Int32 RandomService::Range(Int32 min, Int32 max) const
    {
        EngineBits bits;
        return RandomMapping::RangeInt(bits, min, max);
    }

    Float RandomService::Range(Float min, Float max) const
    {
        EngineBits bits;
        return RandomMapping::RangeFloat(bits, min, max);
    }

    Bool RandomService::Chance(Float probability) const
    {
        EngineBits bits;
        return RandomMapping::Chance(bits, probability);
    }

    UInt64 RandomService::GetSeed() const
    {
        const System::IRandomSystem* system = GetSystemContext().Random;
        return system != nullptr ? system->GetSeed() : UInt64(0);
    }

    void RandomService::SetSeed(UInt64 seed) const
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
        const UInt64 seedHigh = bits();
        const UInt64 seed = (seedHigh << 32) | bits();
        const UInt64 streamHigh = bits();
        const UInt64 stream = (streamHigh << 32) | bits();
        return RandomStream(seed, stream);
    }
}
