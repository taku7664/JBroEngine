#pragma once

#include <JBro/Runtime/IRandomSystem.h>

#include <cstdint>

namespace JBro::System
{
    // 엔진 난수 흐름의 호스트 구현이다(D-231, 기존 `CRandomService`). `EngineInstance` 가 소유한다.
    //
    // 기존 엔진과 다른 것:
    //   - `std::mt19937` 와 표준 분포 대신 `RandomStream`(PCG32)과 제 매핑이다. 같은 씨앗이면 컴파일러가 달라도 같은 수열이다.
    //   - 잠그지 않는다. 메인 스레드 전용이고 워커는 제 흐름을 든다.
    //   - 씨앗을 로그에 남긴다. 그 수를 프로젝트의 `RandomSeed` 에 적으면 같은 게임을 다시 본다.
    class RandomSystem final : public IRandomSystem
    {
    public:
        RandomSystem();

        // 0 이면 새 씨앗을 뽑는다. 어느 쪽이든 씨앗을 로그에 남긴다. 에디터는 재생을 시작할 때마다, 게임은 켤 때 한 번 부른다.
        void Reseed(std::uint64_t configuredSeed);
        // 시계와 기계의 엔트로피에서 뽑는다. 0 은 주지 않는다 - 0 은 "새로 뽑아라" 의 뜻이다.
        static std::uint64_t MakeEntropySeed();

        std::uint32_t NextUInt32() override;
        std::uint64_t GetSeed() const override;
        void SetSeed(std::uint64_t seed) override;
        RandomState GetState() const override;
        void SetState(const RandomState& state) override;

    private:
        RandomStream m_stream;
        std::uint64_t m_seed = 0;
    };
}
