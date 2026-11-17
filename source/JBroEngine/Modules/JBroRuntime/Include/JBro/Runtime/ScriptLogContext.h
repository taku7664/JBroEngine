#pragma once

#include <JBro/Core/Log.h>
#include <JBro/Runtime/ScriptModule.h>

#include <type_traits>

namespace JBro
{
    // 게임 DLL 의 엔진 코드(정적 링크 사본)가 호스트 로그에 쓰는 길이다(D-380). 로그는 호스트의 것이다 - DLL 사본의 `Log` 고리는
    // 아무도 보지 않으므로, DLL 안에서는 이 블록의 함수 포인터로 호스트 `Log::WriteGrouped` 에 쓴다. 호스트 프로세스 안(블록을
    // 묶지 않은 쪽)에서는 같은 함수가 곧바로 `Log` 에 쓴다.
    //
    // 선택 블록이다. 모듈이 요구 목록에 넣지 않으므로 이 블록이 없는 호스트·DLL 과도 실린다 - 그때는 그 모듈 자신의 `Log` 에 쓴다
    // (호스트 프로세스에서는 그것이 호스트 로그다).
    // `ScriptErrorContext` 는 콜백을 멈추는 스크립트 오류(오류 수준 고정)이고, 이것은 수준을 고르는 알림이다.
    inline constexpr UInt32 ScriptLogContextAbiVersion = 1;
    inline constexpr ScriptContextTypeId ScriptLogContextTypeId = MakeStableTypeId("JBro.Runtime.ScriptLogContext");

    struct ScriptLogContext
    {
        // `level` 은 `LogLevel` 의 값이다. 문자열은 부르는 동안만 산다. 예외를 넘기지 않는다.
        using WriteFunction = void (*)(void* owner, UInt32 level, const char* category, const char* groupKey,
            const char* message) noexcept;

        UInt32 AbiVersion = ScriptLogContextAbiVersion;
        UInt32 StructSize = static_cast<UInt32>(sizeof(ScriptLogContext));
        void* Owner = nullptr;
        WriteFunction Write = nullptr;
    };

    ScriptContextBlock MakeScriptLogContextBlock(const ScriptLogContext& context) noexcept;
    const ScriptLogContext* FindScriptLogContext(const ScriptModuleLoadContext& context) noexcept;
    void BindScriptLogContext(const ScriptLogContext& context) noexcept;

    // 묶은 블록이 있으면 그리로, 없으면 이 프로세스의 `Log` 로 쓴다. `groupKey` 가 같으면 로그 창에서 한 줄로 묶인다 -
    // 매 프레임 같은 자리에서 나는 알림이 줄을 덮지 않게 자리를 키로 준다.
    void WriteEngineLog(LogLevel level, const char* category, const char* groupKey, const char* message) noexcept;

    static_assert(std::is_standard_layout_v<ScriptLogContext>);
    static_assert(std::is_trivially_copyable_v<ScriptLogContext>);
    static_assert(sizeof(void*) != 8 || sizeof(ScriptLogContext) == 24);
}
