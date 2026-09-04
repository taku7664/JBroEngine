#include <JBro/Core/InstanceIdGenerator.h>

#include <chrono>
#include <random>

namespace JBro
{
    namespace
    {
        constexpr std::uint64_t TimestampMask = (1ull << 42) - 1;
        constexpr std::uint64_t SessionMask = (1ull << 10) - 1;
        constexpr std::uint32_t SequenceCapacity = 1u << 12;

        std::uint32_t GenerateSessionRandom()
        {
            std::random_device source;
            std::mt19937 engine(source());
            std::uniform_int_distribution<std::uint32_t> distribution(
                0,
                static_cast<std::uint32_t>(SessionMask));
            return distribution(engine);
        }

        std::uint64_t CurrentMilliseconds()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }
    }

    void InstanceIdGenerator::BeginFrame()
    {
        const std::uint64_t currentMs = CurrentMilliseconds();
        if (currentMs > m_cachedMs)
        {
            m_cachedMs = currentMs;
        }
        else
        {
            ++m_cachedMs;
        }

        m_sequence = 0;
        if (false == m_sessionInitialized)
        {
            m_session = GenerateSessionRandom();
            m_sessionInitialized = true;
        }
    }

    InstanceId InstanceIdGenerator::Generate()
    {
        if (m_cachedMs == 0 || false == m_sessionInitialized)
        {
            BeginFrame();
        }

        if (m_sequence == SequenceCapacity)
        {
            ++m_cachedMs;
            m_sequence = 0;
        }

        const std::uint64_t timestamp = m_cachedMs & TimestampMask;
        const std::uint64_t session = m_session & SessionMask;
        const std::uint64_t sequence = m_sequence++;
        return (timestamp << 22) | (session << 12) | sequence;
    }
}
