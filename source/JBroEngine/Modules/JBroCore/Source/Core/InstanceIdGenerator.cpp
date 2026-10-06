#include <JBro/Core/InstanceIdGenerator.h>

#include <chrono>
#include <random>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr UInt64 TimestampMask = (1ull << 42) - 1;
        constexpr UInt64 SessionMask = (1ull << 10) - 1;
        constexpr UInt32 SequenceCapacity = 1u << 12;

        UInt32 GenerateSessionRandom()
        {
            std::random_device source;
            std::mt19937 engine(source());
            std::uniform_int_distribution<std::uint32_t> distribution(
                0,
                static_cast<std::uint32_t>(SessionMask));
            return distribution(engine);
        }

        UInt64 CurrentMilliseconds()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }
    }

    void InstanceIdGenerator::BeginFrame()
    {
        const UInt64 currentMs = CurrentMilliseconds();
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

        const UInt64 timestamp = m_cachedMs & TimestampMask;
        const UInt64 session = m_session & SessionMask;
        const UInt64 sequence = m_sequence++;
        return (timestamp << 22) | (session << 12) | sequence;
    }
}
