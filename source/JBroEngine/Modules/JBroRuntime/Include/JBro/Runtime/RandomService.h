#pragma once

#include <JBro/Core/RandomStream.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 스크립트가 엔진 난수 흐름에서 뽑는 표면이다(D-242).
    //
    //     const auto& random = GetServiceContext().Random;
    //     const std::int32_t damage = random.Range(3, 7);    // 3..7
    //     const float angle = random.Range(0.0f, 6.2831853f); // [0, 2π)
    //     RandomStream enemy = random.MakeStream();           // 제 흐름
    //
    // **씨앗이 정해지면 수열도 정해진다.** 프로젝트의 `RandomSeed` 가 0 이면 재생을 시작할 때마다 새 씨앗이고 그 수가 로그에 남는다.
    // 한 흐름을 여럿이 나눠 쓰면 새 효과 하나가 다른 것의 수를 밀어내므로, 결과가 재현되어야 하는 것은 `MakeStream` 으로 제 흐름을 든다.
    //
    // Main-thread only. 가상 함수가 없다. 묶인 난수 시스템이 없으면(에디터 밖 도구) 이 모듈 사본의 고정 씨앗 흐름에서 뽑는다.
    class RandomService
    {
    public:
        // 이 함수의 이름이 타입 이름과 같아 클래스 안에서 `UInt32` 는 이 함수를 가리킨다. 타입은 `JBro::UInt32` 로 적는다.
        JBro::UInt32 UInt32() const;
        // [0, 1) 이다.
        Float Value() const;
        // [min, max] 의 정수다. min 이 크면 둘을 바꾼다.
        Int32 Range(Int32 min, Int32 max) const;
        // [min, max) 의 실수다. min 이 크면 둘을 바꾼다.
        Float Range(Float min, Float max) const;
        // 참일 확률이 p 다.
        Bool Chance(Float probability) const;

        UInt64 GetSeed() const;
        // 흐름을 이 씨앗으로 다시 세운다.
        void SetSeed(UInt64 seed) const;
        RandomState GetState() const;
        void SetState(const RandomState& state) const;
        // 엔진 흐름에서 씨앗과 흐름 번호를 뽑아 새 흐름을 만든다. 엔진 흐름을 두 번 뽑는다.
        RandomStream MakeStream() const;
    };
}
