#pragma once

#include <JBro/SaveTypes/System/ISaveStorage.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <JBro/Types/UInt.h>

namespace JBro
{
    inline constexpr UInt32 SaveSystemContextAbiVersion = 1;

    // 세이브 저장소 인터페이스 묶음이다. 호스트가 소유한 저장소를 가리킨다.
    // 입력처럼 D-37 확장 블록으로 넘긴다(D-218) - Runtime 이 세이브 모듈을 알 필요가 없다. 서비스 구현만 읽는다.
    struct SaveSystemContext
    {
        UInt32 AbiVersion = SaveSystemContextAbiVersion;
        System::ISaveStorage* Storage = nullptr;
    };

    static_assert(std::is_standard_layout_v<SaveSystemContext>);
    static_assert(std::is_trivially_copyable_v<SaveSystemContext>);
    static_assert(offsetof(SaveSystemContext, AbiVersion) == 0);

    // Main-thread only. 호스트의 값을 이 모듈 사본에 복사한다.
    void BindSaveSystemContext(const SaveSystemContext& context);
    const SaveSystemContext& GetSaveSystems();
}
