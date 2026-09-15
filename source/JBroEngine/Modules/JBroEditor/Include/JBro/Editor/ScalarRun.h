#pragma once

#include <cstdint>

namespace JBro
{
    struct TypeDescriptor;

    // 한 줄에 담기는 실수 묶음이다. `Vec2`·`Rect`·`Color` 처럼 **잎사귀가 전부
    // 실수인 작은 구조체**를 한 줄로 그리고, 여럿 고른 대상에 델타로 옮기는 데 쓴다.
    //
    // 주소를 모으는 이유는 **메모리 배치를 가정하지 않기 위해서다.** 실수 네 개가
    // 붙어 있으리라 믿고 포인터 하나를 `DragFloat4` 에 넘기면, 언젠가 필드 사이에
    // 패딩이 낀 타입에서 엉뚱한 자리를 쓴다.
    //
    // 인스펙터 안에만 있다가 목록 편집(D-86)도 같은 셈을 하게 되어 떼어 냈다.
    struct ScalarRun
    {
        static constexpr std::uint32_t MaxCount = 4;
        float* values[MaxCount] = {};
        std::uint32_t count = 0;
    };

    // 타입의 잎사귀가 전부 실수이고 둘 이상 넷 이하면 모아서 참을 돌려준다.
    bool CollectScalarRun(const TypeDescriptor& type, void* address, ScalarRun& run);

    // 주소 없이 **타입만으로** 한 줄 숫자 묶음인지 본다. 원소가 하나도 없는 목록도 원소의
    // 타입으로 줄 배치를 정해야 한다 - 원소가 생기는 순간 배치가 바뀌면 화면이 튄다(D-89).
    bool IsScalarRunType(const TypeDescriptor& type);

    // 델타를 줄 수 있는 값인가. 실수 묶음이거나 실수 하나면 참이고, 그 주소를 모은다.
    bool CollectNumbers(const TypeDescriptor& type, void* address, ScalarRun& run);
}
