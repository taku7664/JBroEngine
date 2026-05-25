#include "pch.h"

#include "Core/Game/IGameModule.h"
#include "Core/Game/GameModuleTypes.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"

// 등록할 스크립트 헤더 추가
#include "Scripts/ExampleScript.h"

// ── GameCodeModule ─────────────────────────────────────────────────────────────
// 이 파일이 게임 모듈의 진입점입니다.
// Initialize() 에서 스크립트를 등록하고, Finalize() 에서 등록을 해제합니다.
class GameCodeModule final : public IGameModule
{
public:
    bool Initialize(const GameModuleContext& context) override
    {
        m_registry = context.Reflection;
        if (nullptr == m_registry)
        {
            return false;
        }

        // ── 스크립트 등록 ────────────────────────────────────────────────────
        // 새로운 스크립트를 추가할 때마다 아래에 RegisterScript 한 줄을 추가하세요.
        m_registry->RegisterScript<ExampleScript>({
            "ExampleScript",        // 타입 이름 (고유해야 함)
            "Example Script",       // 에디터 표시 이름
            "GameCode"              // 카테고리
        });

        return true;
    }

    void Tick() override
    {
        // 스크립트 틱은 ScriptSystem이 각 ScriptComponent를 순회하며 처리합니다.
        // 여기서는 모듈 수준의 전역 로직이 있을 경우에만 구현합니다.
    }

    void Finalize() override
    {
        if (nullptr == m_registry)
        {
            return;
        }

        // ── 스크립트 등록 해제 ───────────────────────────────────────────────
        // Initialize()에서 등록한 순서와 반대 순서로 해제합니다.
        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("ExampleScript"));

        m_registry = nullptr;
    }

    const GameModuleDesc& GetDesc() const override
    {
        static const GameModuleDesc desc{ "GameCode", "1.0.0" };
        return desc;
    }

private:
    CReflectionRegistry* m_registry = nullptr;
};

// ── DLL 진입점 ────────────────────────────────────────────────────────────────

extern "C" __declspec(dllexport)
IGameModule* CreateGameModule()
{
    return new GameCodeModule();
}

extern "C" __declspec(dllexport)
void DestroyGameModule(IGameModule* module)
{
    delete module;
}
