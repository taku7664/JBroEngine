#include <JBro/Host/RandomSystem.h>

#include <JBro/Core/Log.h>

#include <chrono>
#include <random>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    RandomSystem::RandomSystem()
    {
        SetSeed(MakeEntropySeed());
    }

    void RandomSystem::Reseed(UInt64 configuredSeed)
    {
        SetSeed(configuredSeed != 0 ? configuredSeed : MakeEntropySeed());
        Log::Write(LogLevel::Info, "random", "random seed: %llu", static_cast<unsigned long long>(m_seed));
    }

    UInt64 RandomSystem::MakeEntropySeed()
    {
        std::random_device device;
        const UInt64 high = device();
        UInt64 seed = (high << 32) | device();
        seed ^= static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return seed != 0 ? seed : UInt64(1);
    }

    UInt32 RandomSystem::NextUInt32()
    {
        return m_stream.NextUInt32();
    }

    UInt64 RandomSystem::GetSeed() const
    {
        return m_seed;
    }

    void RandomSystem::SetSeed(UInt64 seed)
    {
        m_seed = seed;
        m_stream.Seed(seed);
    }

    RandomState RandomSystem::GetState() const
    {
        return m_stream.GetState();
    }

    void RandomSystem::SetState(const RandomState& state)
    {
        m_stream.SetState(state);
    }
}
