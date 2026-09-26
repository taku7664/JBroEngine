#pragma once

#include <cstdint>

namespace JBro::Service
{
    // 스크립트가 시간을 읽는 표면이다(D-231). 훅은 델타를 인자로 받지 않는다(ProjectRule §7) - 여기서 읽는다.
    //
    //     const float dt = GetServiceContext().Time.DeltaTime();
    //
    // **고정 스텝 안에서는 고정 델타와 고정 시간이다.** `OnFixedUpdate` 에서 `DeltaTime()` 을 읽으면 프레임 델타가 아니라 고정 델타가
    // 나온다(기존 엔진은 프레임 델타가 나왔다, time-plan T3). 바꿀 수 있는 것은 타임스케일뿐이다 - 고정 델타와 상한은 프로젝트 설정이다.
    //
    // 가상 함수를 두지 않는다(서비스는 컨텍스트 안에 값으로 들어가 DLL 경계를 넘는다). Main-thread only. 묶인 시간 시스템이 없으면
    // 델타 0·타임스케일 1 인 멈춘 시간이다.
    class TimeService
    {
    public:
        // 게임 델타(초)다. 고정 스텝 안이면 고정 델타, 멈춰 있으면 0 이다.
        float DeltaTime() const;
        // 타임스케일과 멈춤을 타지 않는 델타다. 멈춘 메뉴의 애니메이션이 쓴다. 고정 스텝 안에서도 프레임의 것이다.
        float UnscaledDeltaTime() const;
        float FixedDeltaTime() const;
        // 재생을 시작한 뒤의 게임 시간(초)이다. 고정 스텝 안이면 그 스텝의 고정 시간이다.
        double Time() const;
        double UnscaledTime() const;
        double FixedTime() const;
        std::uint64_t FrameCount() const;
        float TimeScale() const;
        // 0 이상 100 이하. 벗어나거나 NaN 이면 거짓이고 그대로 둔다. 0 이면 게임 시간이 서고 고정 스텝이 돌지 않는다.
        bool SetTimeScale(float scale) const;
        bool IsInFixedStep() const;
        // 고정 스텝을 다 돈 뒤 남은 몫이다(0 이상 1 미만). 앞 스텝과 지금 스텝의 상태를 이만큼 섞어 그리면 매끄럽다.
        float FixedStepAlpha() const;
        // 게임이 멈춰 있는가(에디터의 일시정지). 한 프레임 진행 중에도 참이다.
        bool IsPaused() const;
    };
}
