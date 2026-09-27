#pragma once

#include <JBro/Host/IFramework.h>
#include <JBro/Host/TimeSystem.h>

namespace JBro::Testing
{
    // 호스트 없이 프레임워크를 돌리는 시험의 시계다(D-241). 호스트는 `EngineInstance` 가 시계를 들고 프레임마다 `BeginFrame` 을
    // 부르지만, 프레임워크를 직접 세우는 시험에는 그 자리가 없다. 시계는 시험 프로세스에 하나이고 공통 시스템 컨텍스트에 묶인다 -
    // 스크립트가 서비스로 읽는 시간이 이것이다.
    System::TimeSystem& SharedClock();

    // 시계를 처음 상태(기본 설정, 멈추지 않음, 게임 시간 0)로 되돌리고 컨텍스트에 건다. 공통 시스템 컨텍스트에도 다시 묶는다 -
    // 앞의 엔진 시험이 내리면서 비웠을 수 있다.
    void AttachClock(FrameworkContext& context);
    void AttachClock(FrameworkContext& context, const TimeSettings& settings);

    // 시계를 연 뒤 프레임워크를 한 프레임 돌린다. 호스트의 `TickFrame` 과 같은 순서다.
    void Tick(IFramework& framework, float deltaTime);
}
