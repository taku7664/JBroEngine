#include "pch.h"

#include "Core/ScriptCore.h"
#include "Core/ScriptCore.h"
#include "Core/Game/GameModuleTypes.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameModuleEntry.h"

// ── 스크립트 헤더 추가 ────────────────────────────────────────────────────────
// Scripts/ 폴더에 새 스크립트를 추가하면 여기에 include 한 줄만 추가하세요.
// 등록(RegisterScript) / 해제(UnregisterScript) 는 아래 코드가 자동 처리합니다.
#include "Scripts/DefaultScript.h"

// ── GameScriptSampleModule ───────────────────────────────────────────────────────
// DLL 진입점 모듈. Initialize() 에서 스크립트를 등록하고
// Finalize() 에서 등록을 해제합니다.
//
// SCRIPT_CLASS + REFLECT_FIELD 를 사용한 스크립트는
// RegisterScript<T>() 호출 한 줄이면 프로퍼티가 자동으로 Inspector 에 노출됩니다.
class GameScriptSampleModule final : public IGameModule
{
public:
    bool Initialize(const GameModuleContext& context) override
    {
        BindScriptCore(context.HostScriptCore);

        m_registry = Script.Reflection.TryGet();
        if (nullptr == m_registry)
        {
            UnbindScriptCore();
            return false;
        }

        // ── 스크립트 등록 ────────────────────────────────────────────────────
        // REFLECT_FIELD 가 있는 경우 Properties 가 자동으로 채워집니다.
        m_registry->RegisterScript<CDefaultScript>({
            "CDefaultScript",
            "Default Script",
            "GameScriptSample"
        });

        return true;
    }

    void Tick() override {}

    void Finalize() override
    {
        if (nullptr == m_registry)
        {
            return;
        }

        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("CDefaultScript"));

        m_registry = nullptr;

        // 호스트 객체 댕글링 포인터 방지.
        UnbindScriptCore();
    }

    const GameModuleDesc& GetDesc() const override
    {
        static const GameModuleDesc desc{ "GameScriptSample", "1.0.0" };
        return desc;
    }

private:
    CReflectionRegistry* m_registry = nullptr;
};

// ── DLL 진입점 ────────────────────────────────────────────────────────────────

extern "C" GAMESCRIPT_API
IGameModule* CreateGameModule(const GameModuleHostApi* hostApi)
{
    if (nullptr == hostApi || nullptr == hostApi->Allocate)
    {
        return nullptr;
    }

    void* memory = hostApi->Allocate(sizeof(GameScriptSampleModule), alignof(GameScriptSampleModule));
    return memory ? new (memory) GameScriptSampleModule() : nullptr;
}

extern "C" GAMESCRIPT_API
void DestroyGameModule(IGameModule* module, const GameModuleHostApi* hostApi)
{
    if (nullptr == module)
    {
        return;
    }

    GameScriptSampleModule* typedModule = static_cast<GameScriptSampleModule*>(module);
    typedModule->~GameScriptSampleModule();
    if (hostApi && hostApi->Free)
    {
        hostApi->Free(typedModule, sizeof(GameScriptSampleModule), alignof(GameScriptSampleModule));
    }
}
