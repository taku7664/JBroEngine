#pragma once

#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 이번 프레임의 시간이다(D-241). 호스트의 `System::TimeSystem` 이 프레임마다 한 번 채우고, 서비스가 읽는다.
    // 스크립트는 이것을 직접 보지 않고 `Service::TimeService` 를 쓴다 - 고정 스텝 안에서 델타가 무엇인지는 서비스가 고른다.
    struct FrameTime
    {
        // 이번 프레임의 게임 델타(초)다. 언스케일 델타 × 타임스케일이고, 멈춰 있으면 0 이다.
        float deltaTime = 0.0f;
        // 자른 뒤의 실제 델타(초)다. 멈춰도 흐른다. 상한은 프로젝트의 `MaxDeltaTime` 이다.
        float unscaledDeltaTime = 0.0f;
        float fixedDeltaTime = 1.0f / 60.0f;
        float timeScale = 1.0f;
        // 고정 스텝을 다 돈 뒤 누산기에 남은 몫이다(0 이상 1 미만). 렌더 보간을 할 게임이 쓴다.
        float fixedStepAlpha = 0.0f;
        // 이번 프레임에 돌 고정 스텝 수다.
        std::uint32_t fixedStepCount = 0;
        // 재생을 시작한 뒤 흐른 게임 시간이다. 한 시간 뒤에도 마이크로초가 남도록 double 이다(time-plan T2).
        double time = 0.0;
        // 엔진이 선 뒤 흐른 실제 시간이다.
        double unscaledTime = 0.0;
        // 마지막 고정 스텝(스텝 안이면 지금 스텝)이 끝나는 게임 시간이다.
        double fixedTime = 0.0;
        // 엔진이 선 뒤의 프레임 수다. 재생을 다시 시작해도 되돌리지 않는다.
        std::uint64_t frameCount = 0;
        // 고정 스텝을 도는 중이다. 이때 서비스의 델타는 고정 델타다.
        bool inFixedStep = false;
        // 게임이 멈춰 있다(에디터의 일시정지·편집). 한 프레임 진행은 멈춘 채로 도는 프레임이다.
        bool paused = false;
        // 이번 프레임이 멈춘 게임의 한 프레임 진행이다.
        bool stepFrame = false;
    };

    static_assert(std::is_standard_layout_v<FrameTime>);
    static_assert(std::is_trivially_copyable_v<FrameTime>);
}

namespace JBro::System
{
    // 서비스가 시간을 읽고 타임스케일을 바꾸는 길이다(D-241). 호스트의 `TimeSystem` 이 구현한다.
    // 가상 함수 표는 스크립트 DLL 과의 ABI 다. 바꾸면 공통 `SystemContext` 의 판번호를 올린다(D-28).
    class ITimeSystem
    {
    public:
        virtual ~ITimeSystem() = default;

        virtual const FrameTime& GetFrameTime() const = 0;
        // 0 이상 100 이하만 받는다. NaN·범위 밖이면 거짓이고 그대로 둔다. 다음 프레임의 델타부터 걸린다.
        virtual bool SetTimeScale(float scale) = 0;
    };
}
