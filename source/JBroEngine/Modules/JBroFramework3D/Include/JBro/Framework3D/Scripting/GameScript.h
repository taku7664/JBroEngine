#pragma once

#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <type_traits>

namespace JBro
{
    // 3D 스크립트 모듈이 타입을 호스트에 알리는 길이다(cpp-script-plan §3.2). 3D 에는 아직 차원 훅(충돌 등)이 없어
    // 스크립트가 `GameScriptBase` 에서 바로 파생한다. 훅이 생기면 2D 의 `GameScript2D` 처럼 기반 타입을 두고 여기서 검사한다.
    template<typename T>
    bool RegisterScriptType3D()
    {
        static_assert(std::is_base_of_v<GameScriptBase, T>,
            "a 3D script must derive from JBro::GameScriptBase");
        return RegisterScriptType<T>();
    }
}

// 3D 스크립트 하나를 등록한다. 그 스크립트의 `.cpp` 에 한 줄 쓴다(2D 의 `JBRO_REGISTER_SCRIPT_2D` 와 같다).
#define JBRO_REGISTER_SCRIPT_3D(Type)                                                          \
    static ::JBro::ScriptTypeRegistration JBRO_SCRIPT_CONCAT(JBroScriptRegistration_, __LINE__)( \
        []() -> bool                                                                           \
        {                                                                                      \
            return ::JBro::RegisterScriptType3D<Type>();                                       \
        })
