#include <JBro/Host/RandomSystem.h>

#include <JBro/Core/Log.h>

#include <chrono>
#include <random>

namespace JBro::System
{
    RandomSystem::RandomSystem()
    {
        SetSeed(MakeEntropySeed());
    }

    void RandomSystem::Reseed(std::uint64_t configuredSeed)
    {
        SetSeed(configuredSeed != 0 ? configuredSeed : MakeEntropySeed());
        Log::Write(LogLevel::Info, "random", "random seed: %llu", static_cast<unsigned long long>(m_seed));
    }

    std::uint64_t RandomSystem::MakeEntropySeed()
    {
        std::random_device device;
        const std::uint64_t high = device();
        std::uint64_t seed = (high << 32) | device();
        seed ^= static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return seed != 0 ? seed : 1;
    }

    std::uint32_t RandomSystem::NextUInt32()
    {
        return m_stream.NextUInt32();
    }

    std::uint64_t RandomSystem::GetSeed() const
    {
        return m_seed;
    }

    void RandomSystem::SetSeed(std::uint64_t seed)
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
