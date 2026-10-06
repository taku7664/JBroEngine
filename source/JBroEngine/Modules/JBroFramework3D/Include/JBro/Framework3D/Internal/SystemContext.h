#pragma once

#include <JBro/Runtime/ITextSystem.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 1: 텍스트 시스템이 첫 슬롯이다(D-224).
    inline constexpr UInt32 Framework3DSystemContextAbiVersion = 1;

    // 3D 시스템 인터페이스 묶음이다. 2D 의 `Framework2DSystemContext` 와 같이 D-37 확장 Context 로 전달되고 Internal 경계에 둔다.
    // 서비스 구현만 읽으며, 대상의 수명은 소유하지 않는다.
    struct Framework3DSystemContext
    {
        UInt32 AbiVersion = Framework3DSystemContextAbiVersion;
        System::ITextSystem* Text3D = nullptr;
    };

    static_assert(std::is_standard_layout_v<Framework3DSystemContext>);
    static_assert(std::is_trivially_copyable_v<Framework3DSystemContext>);
    static_assert(offsetof(Framework3DSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindFramework3DSystemContext(const Framework3DSystemContext& context);
    const Framework3DSystemContext& GetFramework3DSystems();
}
