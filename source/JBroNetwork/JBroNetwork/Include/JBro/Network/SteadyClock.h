#pragma once

#include <JBro/Network/Socket.h>

#include <chrono>

namespace JBro::Network
{
    // 실제 단조 시계다. 호스트가 트랜스포트에 넘긴다. 테스트는 `Testing::ManualClock` 을 쓴다.
    class SteadyClock final : public IClock
    {
    public:
        double NowMilliseconds() const override
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return std::chrono::duration<double, std::milli>(now).count();
        }
    };
}
