#pragma once

#include <JBro/Core/Core.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // [ 42비트 ms ][ 10비트 세션 난수 ][ 12비트 시퀀스 ]
    // 상태를 가지므로 Core.h 가 아니라 별도 헤더에 둔다.
    class InstanceIdGenerator
    {
    public:
        void       BeginFrame();          // 타임스탬프를 프레임당 1회만 읽는다
        InstanceId Generate();            // 실질 비용은 ++m_sequence

    private:
        UInt64 m_cachedMs = 0;
        UInt32 m_session  = 0;     // 프로세스 시작 시 1회 난수
        UInt32 m_sequence = 0;
        Bool          m_sessionInitialized = false;
    };
}
