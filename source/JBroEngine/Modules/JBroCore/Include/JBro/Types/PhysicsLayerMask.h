#pragma once

#include <JBro/Types/UInt.h>

namespace JBro
{
    // 물리 레이어 비트 묶음이다(D-233). 2D 와 3D 가 함께 쓴다 - 차원과 무관한 값 타입이라 Core 에 한 번 둔다(D-416, 전에는 Framework2D 에 있었다).
    // 메모리와 파일은 부호 없는 32 비트 정수 그대로이고(옛 파일과 같은 글자), 이름은 프로젝트 설정의 `PhysicsLayers` 가 준다 -
    // 비트 i 가 그 목록의 i 번째 이름이다. 에디터는 이 타입 이름을 보고 이름을 고르는 칸을 그린다. 정수와 저절로 오가므로
    // 스크립트는 `collider->layer = JBro::UInt32(1u << 3);` 처럼 쓴다. 리플렉션은 `JBro/Reflection/PhysicsLayerMaskReflection.h` 다.
    struct PhysicsLayerMask
    {
        UInt32 bits = 0;

        constexpr PhysicsLayerMask() = default;
        constexpr PhysicsLayerMask(UInt32 value)
            : bits(value)
        {
        }

        constexpr operator UInt32() const
        {
            return bits;
        }

        friend constexpr bool operator==(PhysicsLayerMask left, PhysicsLayerMask right)
        {
            return left.bits == right.bits;
        }
    };

    // 프로젝트가 담는 물리 레이어 수다. 비트 하나가 레이어 하나다.
    inline constexpr UInt32 PhysicsLayerCount = 32;

    // 질의가 모든 레이어를 본다(콜라이더의 `layer` 비트와 AND 해서 0 이 아니면 대상이다).
    inline constexpr UInt32 AllPhysicsLayers = 0xFFFFFFFFu;
}
