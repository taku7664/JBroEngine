#pragma once

#include <JBro/Runtime/IDebugDrawSystem.h>
#include <JBro/Runtime/IMaterialSystem.h>
#include <JBro/Runtime/IObjectSystem.h>
#include <JBro/Runtime/IPostProcessSystem.h>
#include <JBro/Runtime/IRandomSystem.h>
#include <JBro/Runtime/ITimeSystem.h>
#include <JBro/Runtime/IViewportSystem.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 4: 시간과 난수 시스템이 들어왔다(D-242).
    // 5: 디버그 드로 시스템이 들어왔다(D-243).
    // 7: 후처리 덮어쓰기 시스템이 들어왔다(D-346).
    // 8: 머티리얼 파라미터 덮어쓰기 시스템이 들어왔다(D-348).
    // 9: 그 시스템이 오브젝트 하나의 파라미터를 찾는다(D-368).
    // 10: 캔버스 뷰포트 목록 시스템이 들어왔다(D-438).
    inline constexpr UInt32 SystemContextAbiVersion = 10;

    // 차원과 무관한 시스템만 담는다. 차원별 시스템은 각 Framework 가 확장 Context 블록으로
    // 전달한다(D-43) — 여기에 두면 3D 게임 바이너리의 공통 Context 에 2D 슬롯이 남는다.
    // 호스트(`EngineInstance`)가 소유한 것을 가리킨다. 서비스 구현만 읽는다.
    struct SystemContext
    {
        UInt32 AbiVersion = SystemContextAbiVersion;
        System::ITimeSystem* Time = nullptr;
        System::IRandomSystem* Random = nullptr;
        System::IDebugDrawSystem* DebugDraw = nullptr;
        // 오브젝트 만들기·찾기(D-301). 캔버스를 가진 프레임워크가 스크립트 컨텍스트를 묶을 때 꽂는다. 없으면 서비스가 빈 핸들을 준다.
        System::IObjectSystem* Objects = nullptr;
        // 후처리 필드 덮어쓰기(D-346). 오브젝트와 같이 캔버스를 가진 프레임워크가 꽂는다. 없으면 서비스가 아무것도 하지 않는다.
        System::IPostProcessSystem* PostProcess = nullptr;
        // 머티리얼 파라미터 덮어쓰기(D-348). 후처리와 같이 캔버스를 가진 프레임워크가 꽂는다. 없으면 서비스가 아무것도 하지 않는다.
        System::IMaterialSystem* Material = nullptr;
        // 캔버스의 뷰포트 목록(D-438). 오브젝트와 같이 캔버스를 가진 프레임워크가 꽂는다. 없으면 서비스가 빈 목록이다.
        System::IViewportSystem* Viewports = nullptr;
    };

    static_assert(std::is_standard_layout_v<SystemContext>);
    static_assert(std::is_trivially_copyable_v<SystemContext>);
    static_assert(offsetof(SystemContext, AbiVersion) == 0);

    void BindSystemContext(const SystemContext& context);
    const SystemContext& GetSystemContext();
}
