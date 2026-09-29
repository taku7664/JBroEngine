#pragma once

#include <cstdint>

namespace JBro
{
    struct ScriptModuleApi;

    namespace Internal
    {
        // 3D 스크립트 모듈의 API 다(cpp-script-plan §3.2). 컨텍스트 검증·바인딩·해제와 스크립트 타입 등록이 여기에 있다 -
        // 구현이 JBroFramework3D 안에 있으므로 서비스가 늘어도 사용자 DLL 은 다시 빌드만 하면 되고 코드는 바뀌지 않는다.
        const ScriptModuleApi* GetScriptModuleApi3D(std::uint32_t hostAbiVersion, std::uint32_t hostApiSize) noexcept;
    }
}

// 3D 스크립트 DLL 의 진입점이다. 프로젝트에 한 번, 한 `.cpp` 에만 쓴다(에디터가 프로젝트를 만들 때 `Contents/Scripts/ScriptModule.cpp` 에 둔다).
// 호스트가 찾는 단 하나의 심볼 `JBroScriptModule_GetApi` 를 낸다(ProjectRule §6.2).
#define JBRO_SCRIPT_MODULE_3D()                                                      \
    extern "C" __declspec(dllexport) const ::JBro::ScriptModuleApi* JBroScriptModule_GetApi( \
        std::uint32_t hostAbiVersion,                                                  \
        std::uint32_t hostApiSize) noexcept                                            \
    {                                                                                  \
        return ::JBro::Internal::GetScriptModuleApi3D(hostAbiVersion, hostApiSize); \
    }
