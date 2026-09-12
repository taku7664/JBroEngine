#pragma once

#include <cstdint>

namespace JBro
{
    inline constexpr std::uint32_t SystemContextAbiVersion = 3;

    // 차원과 무관한 시스템만 담는다. 차원별 시스템은 각 Framework 가 확장 Context 블록으로
    // 전달한다(D-43) — 여기에 두면 3D 게임 바이너리의 공통 Context 에 2D 슬롯이 남는다.
    struct SystemContext
    {
        std::uint32_t AbiVersion = SystemContextAbiVersion;
    };

    void BindSystemContext(const SystemContext& context);
    const SystemContext& GetSystemContext();
}
