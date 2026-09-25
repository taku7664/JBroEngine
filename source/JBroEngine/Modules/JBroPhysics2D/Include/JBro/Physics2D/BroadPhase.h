#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

#include <cstdint>

// 2D 물리 커널의 브로드페이즈다(D-199, physics-plan §3.4). 기존 엔진의 x 축 쓸기(sweep-and-prune)를
// 조각 단위 상자로 옮겼다.
namespace JBro::Physics2D
{
    struct ProxyPair
    {
        // 입력 배열의 번호. 언제나 first < second 다.
        std::uint32_t first = 0;
        std::uint32_t second = 0;
    };

    // 매 스텝 도는 자리다. 스크래치를 멤버로 두어 용량이 찬 뒤로는 할당하지 않는다.
    class SweepAndPrune
    {
    public:
        // 경계를 포함해 겹치는 상자 쌍을 (first, second) 사전순으로 채운다. 순서가 입력 배치에 흔들리지 않아야
        // 솔버의 순차 반복이 결정적이다. 유한하지 않은 상자는 건너뛴다.
        void FindPairs(ArrayView<const Rect> boxes, Array<ProxyPair>& pairs);

    private:
        struct Proxy
        {
            Rect          box;
            std::uint32_t index = 0;
        };

        Array<Proxy> m_sorted;
        Array<Proxy> m_active;
    };
}
