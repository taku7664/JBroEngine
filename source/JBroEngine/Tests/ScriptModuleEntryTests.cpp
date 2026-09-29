#include <JBro/Framework3D/Internal/ScriptModuleContext.h>
#include <JBro/Framework3D/Scripting/GameScript.h>
#include <JBro/Framework3D/Scripting/ScriptModule.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

// 스크립트 진입점 매크로(cpp-script-plan §3.2)를 호스트 안에서 본다. 2D 는 실제 시험 DLL(`ScriptModuleProbe`)이 쓰고
// `ScriptDLLLoaderTests` 가 싣는다. 3D 는 시험 DLL 이 없어 여기서 API 의 모양과 미뤄 둔 등록만 본다 - `Load` 는 부르지 않는다.
// 호스트 프로세스에서 부르면 공통 컨텍스트를 호스트 자기 것으로 다시 묶고, `Unload` 가 그것을 비운다.

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    class EntryProbe3D final : public JBro::GameScriptBase
    {
        JBRO_SCRIPT_BODY(EntryProbe3D)
    public:
        JBRO_FIELD(float, Height) = 1.5f;
    };
}

// 이 실행 파일의 목록에 걸린다. 등록은 아래 시험이 `RegisterPendingScriptTypes` 를 부를 때 한다.
JBRO_REGISTER_SCRIPT_3D(EntryProbe3D);

namespace
{
    void TestThe3DModuleAsksForThe3DBlocks()
    {
        Check(JBro::Internal::GetScriptModuleApi3D(JBro::ScriptModuleAbiVersion + 1, sizeof(JBro::ScriptModuleApi)) == nullptr,
            "a host with another ABI gets no API");
        const JBro::ScriptModuleApi* api =
            JBro::Internal::GetScriptModuleApi3D(JBro::ScriptModuleAbiVersion, sizeof(JBro::ScriptModuleApi));
        Check(api != nullptr && api->Load != nullptr && api->Unload != nullptr, "the 3D module hands out both hooks");
        Check(api->RequiredContextCount == 2
                && api->RequiredContexts[0].TypeId == JBro::Framework3DServiceContextRequirement.TypeId
                && api->RequiredContexts[1].TypeId == JBro::Framework3DSystemContextRequirement.TypeId,
            "and asks for the 3D service and system blocks, so a 2D host refuses it before running any of its code");
    }

    // **등록은 미뤄졌다가 부를 때 된다.** 정적 초기화 때 하면 DLL 이 호스트 이름표를 묶기 전이라 이름이 DLL 사본에 들어간다(D-44).
    void TestRegistrationWaitsUntilItIsAskedFor()
    {
        const char* const name = EntryProbe3D::StaticTypeName();
        Check(std::strcmp(name, "EntryProbe3D") == 0, "the script body names the type after its class");
        Check(JBro::ScriptRegistry::Get().Find(name) == nullptr, "nothing is registered by static initialization");

        Check(JBro::RegisterPendingScriptTypes(), "the pending registrations go through");
        const JBro::ScriptTypeInfo* type = JBro::ScriptRegistry::Get().Find(name);
        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(name);
        Check(type != nullptr && type->typeId == JBro::MakeStableTypeId(name), "the script type lands in the host table");
        Check(table != nullptr && table->count == 1, "and its field table with it");
        Check(false == JBro::RegisterPendingScriptTypes(), "registering the same module twice is refused, not silently doubled");

        // 이 시험이 등록한 것을 거둔다. 스크립트 DLL 이 내려갈 때와 같다.
        JBro::ScriptRegistry::Local().Clear();
        JBro::PropertyRegistry::ScriptLocal().Clear();
    }
}

int RunScriptModuleEntryTests()
{
    TestThe3DModuleAsksForThe3DBlocks();
    TestRegistrationWaitsUntilItIsAskedFor();
    std::cout << "Script module entry tests passed.\n";
    return 0;
}
