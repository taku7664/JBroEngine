#pragma once

#include <cstddef>

namespace JBro::Diagnostics
{
    // 한 오브젝트에서 타입으로 컴포넌트를 찾은 횟수와, 그 과정에서 후보 참조를 실제로
    // 따라간 횟수다. 후보마다 따라가면 제어 블록이 각기 다른 곳에 있으므로
    // 오브젝트의 컴포넌트 수만큼 캐시 미스가 난다(§9 의 매 프레임 경로).
    //
    // ⚠ 카운터는 모듈마다 별개 사본이다. 정적 링크 경계를 넘어 합산되지 않는다.
    // 테스트는 한 바이너리 안이라 그대로 읽을 수 있고, 스크립트 DLL 의 조회는 따로 센다.
    struct ComponentLookupCounters
    {
        std::size_t lookups = 0;
        std::size_t dereferences = 0;

        static ComponentLookupCounters& Get()
        {
            static ComponentLookupCounters counters;
            return counters;
        }

        static void Reset()
        {
            Get() = {};
        }
    };
}
