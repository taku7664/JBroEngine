#pragma once

#include <JBro/Runtime/ITimeSystem.h>

#include <cstdint>

namespace JBro
{
    // 시간의 프로젝트 설정이다(D-231). `.jproject` 의 `FixedDeltaTime`·`MaxFixedSteps`·`MaxDeltaTime` 이다.
    struct TimeSettings
    {
        // 고정 스텝 하나의 길이(초)다. 0.001 이상 1 이하.
        float fixedDeltaTime = 1.0f / 60.0f;
        // 한 프레임에 도는 고정 스텝의 상한이다. 1 이상 64 이하. 넘친 스텝은 버리고 그만큼 게임 시간도 흐르지 않은 것으로 친다.
        std::uint32_t maxFixedSteps = 4;
        // 한 프레임 델타의 상한(초)이다. 0 보다 크고 10 이하. 창 끌기·중단점 재개의 수 초짜리 델타가 게임을 튕기지 않게 한다
        // (기존 엔진과 같은 0.25).
        float maxDeltaTime = 0.25f;
    };

    namespace System
    {
        // 엔진의 시계다(D-231). 프레임 델타·타임스케일·멈춤·한 프레임 진행·고정 스텝 누산을 **한 자리에** 둔다 - 기존 엔진은 시계와 누산기와
        // 멈춤이 셋으로 갈라져 있었고, 이 엔진은 두 프레임워크가 누산기를 따로 들어 3D 만 멈춤을 무시했다(time-plan T4).
        //
        // 한 프레임:
        //     time.BeginFrame(rawDelta);                       // 호스트
        //     for (i < time.GetFrameTime().fixedStepCount)    // 프레임워크
        //         time.BeginFixedStep(); systems.FixedUpdate(...);
        //     time.EndFixedSteps();
        //     systems.Update(..., time.GetFrameTime().deltaTime);
        //
        // 메인 스레드 전용이다. `EngineInstance` 가 소유한다.
        class TimeSystem final : public ITimeSystem
        {
        public:
            static bool IsValid(const TimeSettings& settings);

            // 틀린 설정이면 거짓이고 그대로 둔다. 누산기는 그대로다 - 다음 프레임부터 새 길이로 센다.
            bool Configure(const TimeSettings& settings);
            const TimeSettings& GetSettings() const;

            // 프레임을 연다. 날 델타가 NaN·음수면 거짓이고 아무것도 바꾸지 않는다.
            bool BeginFrame(float rawDeltaTime);
            // 그 프레임의 고정 스텝 하나를 연다. 고정 시간이 한 스텝 나아가고, 스텝 안의 서비스 델타는 고정 델타다.
            void BeginFixedStep();
            void EndFixedSteps();

            // 멈추면 게임 델타가 0 이고 고정 스텝이 돌지 않는다. 언스케일 시간은 흐른다.
            void SetPaused(bool paused);
            bool IsPaused() const;
            // 멈춘 동안 다음 프레임 하나를 고정 스텝 하나로 돌린다(게임 델타 = 고정 델타). 멈추지 않았으면 아무 일도 없다.
            void RequestStep();
            // 이번 프레임이 한 프레임 진행이다.
            bool IsStepFrame() const;
            // 이번 프레임에 게임이 도는가(멈추지 않았거나 한 프레임 진행이다).
            bool IsSimulating() const;

            // 재생의 처음으로 되돌린다: 게임 시간·고정 시간·누산기·타임스케일(1)·밀린 한 프레임 진행. 프레임 수와 언스케일 시간은 엔진 수명이다.
            void ResetGameTime();

            const FrameTime& GetFrameTime() const override;
            bool SetTimeScale(float scale) override;

        private:
            TimeSettings m_settings;
            FrameTime m_time;
            // 게임 델타를 쌓고 고정 스텝만큼 뺀다. 늘 고정 델타보다 작다.
            double m_accumulator = 0.0;
            bool m_stepRequested = false;
        };
    }
}
