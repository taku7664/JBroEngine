#pragma once

#include <JBro/Runtime/IDebugDrawSystem.h>
#include <JBro/Types/Color.h>

#include <cstdint>

namespace JBro::Internal
{
    // 차원별 디버그 드로 서비스가 도형을 선으로 펼 때 쓰는 묶음이다(D-242). 선을 스택에 64 개씩 모았다가 `AddLines` 한 번으로 넘긴다 -
    // 원 하나가 가상 호출 서른둘이 되지 않는다. 힙을 쓰지 않는다. 프렐류드는 이 헤더를 include 하지 않는다(서비스 `.cpp` 만 쓴다).
    // 시스템이 묶이지 않았으면 모은 것을 버린다.
    class DebugLineBatch
    {
    public:
        DebugLineBatch(const Color& color, float duration, float thickness);
        ~DebugLineBatch();
        DebugLineBatch(const DebugLineBatch&) = delete;
        DebugLineBatch& operator=(const DebugLineBatch&) = delete;

        void Add(float fromX, float fromY, float fromZ, float toX, float toY, float toZ);
        void Flush();

    private:
        static constexpr std::uint32_t Capacity = 64;
        DebugLine m_style;
        DebugLine m_lines[Capacity];
        std::uint32_t m_count = 0;
    };
}
