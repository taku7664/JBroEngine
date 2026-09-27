#pragma once

#include <JBro/Core/RandomStream.h>

#include <cstdint>

namespace JBro::System
{
    // 엔진 난수 흐름이다(D-242). 호스트의 `RandomSystem` 이 `RandomStream` 하나를 들고 구현한다. 서비스는 32 비트만 받아 가고 구간 매핑은
    // `RandomMapping` 으로 제 쪽에서 한다 - 게임 DLL 과 호스트가 같은 식을 쓴다.
    // 가상 함수 표는 스크립트 DLL 과의 ABI 다. 바꾸면 공통 `SystemContext` 의 판번호를 올린다(D-28).
    class IRandomSystem
    {
    public:
        virtual ~IRandomSystem() = default;

        virtual std::uint32_t NextUInt32() = 0;
        // 지금 흐름을 세운 씨앗이다. 로그에 남은 이 수를 프로젝트의 `RandomSeed` 에 적으면 같은 수열을 다시 본다.
        virtual std::uint64_t GetSeed() const = 0;
        virtual void SetSeed(std::uint64_t seed) = 0;
        virtual RandomState GetState() const = 0;
        virtual void SetState(const RandomState& state) = 0;
    };
}
