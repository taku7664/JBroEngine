#pragma once

#include <JBro/Network/Socket.h>

namespace JBro::Network::Testing
{
    // 손으로 돌리는 시계다. RTO 만료·keepalive 타임아웃을 결정적으로 재현한다.
    class ManualClock final : public IClock
    {
    public:
        double NowMilliseconds() const override
        {
            return m_now;
        }

        void Advance(double milliseconds)
        {
            m_now += milliseconds;
        }

        void Set(double milliseconds)
        {
            m_now = milliseconds;
        }

    private:
        double m_now = 0.0;
    };
}
