#pragma once

#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 디버그 선 하나다(D-242). 스크립트의 도형(원·상자·화살표)은 서비스가 이것들로 펴서 넘긴다. 2D 는 z 를 0 으로 쓴다.
    struct DebugLine
    {
        float from[3] = {0.0f, 0.0f, 0.0f};
        float to[3] = {0.0f, 0.0f, 0.0f};
        // RGBA8 이다. 선 수만 개를 담는 저장소라 float 넷(16 B) 대신 4 B 로 둔다. 선 하나가 36 B 다.
        std::uint8_t color[4] = {255, 255, 255, 255};
        // 화면 픽셀 두께다. 뷰마다 그 뷰의 배율로 월드 길이로 바꾼다 - 캔버스 뷰를 당겨도 선은 같은 굵기다. 0.25~64 로 자른다.
        float thickness = 1.0f;
        // 남겨 둘 게임 시간(초)이다. 0 이면 한 프레임이고, 고정 스텝에서 그린 0 초짜리는 다음 고정 스텝까지 남는다.
        float duration = 0.0f;
    };

    static_assert(sizeof(DebugLine) == 36, "the debug line store holds tens of thousands of these - keep it small");
    static_assert(std::is_trivially_copyable_v<DebugLine>);
}

namespace JBro::System
{
    // 서비스가 디버그 선을 쌓는 길이다(D-242). 호스트의 `DebugDrawSystem` 이 구현한다. **쌓기만 있다** - 비우기와 읽기는 엔진의 것이다
    // (기존 엔진은 스크립트가 `Clear()` 로 엔진의 버퍼를 비울 수 있었다).
    // 가상 함수 표는 스크립트 DLL 과의 ABI 다. 바꾸면 공통 `SystemContext` 의 판번호를 올린다(D-28).
    class IDebugDrawSystem
    {
    public:
        virtual ~IDebugDrawSystem() = default;

        // 받은 선 수를 돌려준다. 용량이 차면 나머지는 버리고 세며, 좌표·두께·시간이 유한하지 않은 선은 버린다. 인자는 호출 동안만 읽는다.
        virtual std::uint32_t AddLines(const DebugLine* lines, std::uint32_t count) = 0;
    };
}
