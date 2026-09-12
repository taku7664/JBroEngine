#pragma once

#include <JBro/Framework2D/System/IPhysics2DSystem.h>

#include <cstdint>
#include <type_traits>

namespace JBro
{
    inline constexpr std::uint32_t Framework2DSystemContextAbiVersion = 1;

    // 차원별 시스템 인터페이스 묶음. 공통 SystemContext 는 Framework 타입을 알지 않으므로
    // 이 블록이 D-37 확장 Context 로 전달된다. 사용자에게 공개하지 않기 위해 Internal 경계에 둔다.
    // 서비스 구현만 읽으며, 대상의 수명은 소유하지 않는다.
    struct Framework2DSystemContext
    {
        std::uint32_t AbiVersion = Framework2DSystemContextAbiVersion;
        System::IPhysics2DSystem* Physics2D = nullptr;
    };

    static_assert(std::is_standard_layout_v<Framework2DSystemContext>);
    static_assert(std::is_trivially_copyable_v<Framework2DSystemContext>);
    static_assert(offsetof(Framework2DSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindFramework2DSystemContext(const Framework2DSystemContext& context);
    const Framework2DSystemContext& GetFramework2DSystems();
}
