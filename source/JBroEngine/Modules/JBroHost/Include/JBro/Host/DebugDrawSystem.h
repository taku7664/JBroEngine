#pragma once

#include <JBro/Runtime/IDebugDrawSystem.h>
#include <JBro/Types/Array.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    class TimeSystem;

    // 디버그 선의 저장소다(D-243, 기존 `CDebugDraw2D`). `EngineInstance` 가 소유하고, 두 프레임워크의 렌더 브리지가 뷰마다 읽어
    // 스프라이트·월드 텍스트 사각형으로 그린다.
    //
    // 기존 엔진과 다른 것:
    //   - **용량이 고정이다.** 엔진이 설 때 한 번 잡고, 넘치면 버리고 센다 - 매 프레임 자라지 않는다(기존은 `std::vector` 가 자랐고
    //     그릴 때마다 GPU 버퍼를 새로 만들었다).
    //   - **수명이 셋이다.** 0 초짜리는 한 프레임, 0 보다 크면 게임 시간으로 줄고(멈춘 동안은 남는다), 고정 스텝에서 그린 0 초짜리는
    //     다음 고정 스텝까지 남는다(기존은 고정 스텝이 없는 프레임에 깜빡였다). 게임이 멈춘 프레임에는 한 프레임짜리도 지우지 않는다 -
    //     멈춘 화면에서 마지막 선을 봐야 한다.
    //   - **게임 화면에도 나온다.** 시뮬레이션 뷰에서 보일지는 `SetSimulationViewVisible` 이고(게임 실행은 프로젝트의 `DebugModeEnabled`),
    //     캔버스 뷰는 에디터가 뷰마다 정한다.
    // 메인 스레드 전용이다.
    class DebugDrawSystem final : public IDebugDrawSystem
    {
    public:
        static constexpr UInt32 DefaultCapacity = 16384;

        // 용량을 잡는다. 시계가 있으면 고정 스텝 안에서 그린 선을 알아보고 게임 시간으로 줄인다. 없으면(시험) 모두 한 프레임짜리처럼 산다.
        Bool Initialize(UInt32 capacity, const TimeSystem* time);
        void Shutdown();

        // 프레임을 연다. **시계가 프레임을 연 뒤, 프레임워크가 갱신하기 전**에 부른다 - 지난 프레임의 선을 이번 프레임의 시간으로 거둔다.
        void BeginFrame();
        // 모두 지운다. 에디터가 재생을 시작하고 멈출 때 부른다.
        void Clear();

        UInt32 AddLines(const DebugLine* lines, UInt32 count) override;

        UInt32 GetLineCount() const;
        const DebugLine& GetLine(UInt32 index) const;
        UInt32 GetCapacity() const;
        // 이번 프레임(마지막 `BeginFrame` 뒤)에 용량이 차서 버린 선과 값이 틀려 버린 선이다.
        UInt32 GetDroppedCount() const;
        UInt32 GetRejectedCount() const;

        // 시뮬레이션 뷰(게임 실행의 백버퍼, 에디터의 시뮬레이션 뷰)에 그릴지다. 저장은 늘 한다 - 끄고 켜도 선이 사라지지 않는다.
        void SetSimulationViewVisible(Bool visible);
        Bool IsSimulationViewVisible() const;

    private:
        struct Entry
        {
            DebugLine line;
            Bool fromFixedStep = false;
        };

        Array<Entry> m_entries;
        const TimeSystem* m_time = nullptr;
        UInt32 m_capacity = 0;
        UInt32 m_dropped = 0;
        UInt32 m_rejected = 0;
        Bool m_simulationViewVisible = false;
    };
}
