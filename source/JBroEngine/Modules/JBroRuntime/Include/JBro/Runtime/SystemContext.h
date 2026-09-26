#pragma once

#include <JBro/Runtime/IDebugDrawSystem.h>
#include <JBro/Runtime/IRandomSystem.h>
#include <JBro/Runtime/ITimeSystem.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 4: 시간과 난수 시스템이 들어왔다(D-233).
    // 5: 디버그 드로 시스템이 들어왔다(D-234).
    inline constexpr std::uint32_t SystemContextAbiVersion = 5;

    // 차원과 무관한 시스템만 담는다. 차원별 시스템은 각 Framework 가 확장 Context 블록으로
    // 전달한다(D-43) — 여기에 두면 3D 게임 바이너리의 공통 Context 에 2D 슬롯이 남는다.
    // 호스트(`EngineInstance`)가 소유한 것을 가리킨다. 서비스 구현만 읽는다.
    struct SystemContext
    {
        std::uint32_t AbiVersion = SystemContextAbiVersion;
        System::ITimeSystem* Time = nullptr;
        System::IRandomSystem* Random = nullptr;
        System::IDebugDrawSystem* DebugDraw = nullptr;
    };

    static_assert(std::is_standard_layout_v<SystemContext>);
    static_assert(std::is_trivially_copyable_v<SystemContext>);
    static_assert(offsetof(SystemContext, AbiVersion) == 0);

    void BindSystemContext(const SystemContext& context);
    const SystemContext& GetSystemContext();
}
