#include "pch.h"

#include "Core/ScriptCore.h"
#include "Core/ScriptCore.h"
#include "Core/Game/GameModuleTypes.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameModuleEntry.h"
#include "Utillity/Types/Allocator.h"

// ── 스크립트 헤더 추가 ────────────────────────────────────────────────────────
// Scripts/ 폴더에 새 스크립트를 추가하면 여기에 include 한 줄만 추가하세요.
// 등록(RegisterScript) / 해제(UnregisterScript) 는 아래 코드가 자동 처리합니다.
#include "Scripts/DefaultScript.h"
#include "Scripts/CoroutineTestScript.h"
#include "Scripts/PrefabSpawnTestScript.h"
#include "Scripts/AudioBusProbeScript.h"
#include "Scripts/AnimationProbeScript.h"
#include "Scripts/JointProbeScript.h"
#include "Scripts/QueryProbeScript.h"

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

        m_registry->RegisterScript<CCoroutineTestScript>({
            "CCoroutineTestScript",
            "Coroutine Test",
            "GameScriptSample"
        });

        // 프로퍼티가 없으므로 목록 없이 한 줄로 등록한다.
        m_registry->RegisterScript<CAudioBusProbeScript>({
            "CAudioBusProbeScript",
            "Audio Bus Probe",
            "GameScriptSample"
        });

        m_registry->RegisterScript<CQueryProbeScript>({
            "CQueryProbeScript",
            "Query Probe",
            "GameScriptSample"
        });

        m_registry->RegisterScript<CJointProbeScript>({
            "CJointProbeScript",
            "Joint Probe",
            "GameScriptSample"
        });

        m_registry->RegisterScript<CAnimationProbeScript>({
            "CAnimationProbeScript",
            "Animation Probe",
            "GameScriptSample"
        });

        // 프로퍼티를 인스펙터에 노출하려면 목록을 함께 넘긴다. 에디터의 코드 생성기를 쓰는
        // 프로젝트(JPROP 마커)는 이 목록이 자동 생성되지만, 이 샘플은 손으로 등록하므로
        // 스크립트의 필드를 바꾸면 여기도 같이 고쳐야 한다.
        m_registry->RegisterScript<CPrefabSpawnTestScript>(
            ScriptRegisterDesc{
                "CPrefabSpawnTestScript",
                "Prefab Spawn Test",
                "GameScriptSample"
            },
            std::vector<ScriptPropertyDesc>{
                ScriptPropertyDesc{
                    .Name = "Prefab",
                    .Type = EReflectPropertyType::Ref,
                    .Offset = offsetof(CPrefabSpawnTestScript, Prefab),
                    .Size = sizeof(Ref<CPrefabAsset>),
                    .ElementCount = 1,
                    .DisplayName = "스폰할 프리팹",
                    .Serialize = true,
                    .Descriptor = &GetScalarReflectTypeDesc<Ref<CPrefabAsset>, EReflectPropertyType::Ref>(),
                    .RefCategory = Ref<CPrefabAsset>::Category,
                    .RefTypeName = "CPrefabAsset",
                    .ExpectedAssetType = EAssetType::Prefab
                },
                ScriptPropertyDesc{
                    .Name = "SpawnCount",
                    .Type = EReflectPropertyType::Int64,
                    .Offset = offsetof(CPrefabSpawnTestScript, SpawnCount),
                    .Size = sizeof(Int),
                    .ElementCount = 1,
                    .DisplayName = "한 번에 스폰할 개수",
                    .HasRange = true,
                    .RangeMin = 1.0f,
                    .RangeMax = 32.0f,
                    .Serialize = true,
                    .Descriptor = &GetScalarReflectTypeDesc<Int, EReflectPropertyType::Int64>()
                },
                ScriptPropertyDesc{
                    .Name = "SpawnSpacing",
                    .Type = EReflectPropertyType::Float,
                    .Offset = offsetof(CPrefabSpawnTestScript, SpawnSpacing),
                    .Size = sizeof(Float),
                    .ElementCount = 1,
                    .DisplayName = "스폰 간격",
                    .Serialize = true,
                    .Descriptor = &GetScalarReflectTypeDesc<Float, EReflectPropertyType::Float>()
                }
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
        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("CCoroutineTestScript"));
        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("CPrefabSpawnTestScript"));
        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("CQueryProbeScript"));
        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("CJointProbeScript"));
        m_registry->UnregisterScript(CReflectionRegistry::MakeTypeId("CAnimationProbeScript"));

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

    // Array 등 엔진 컨테이너가 잡는 버퍼를 호스트 힙으로 모은다. 이 DLL 이 링크한 Engine.lib
    // 사본은 자기 CRT 힙을 기본값으로 들고 있어서, 이걸 안 하면 에디터가 인스펙터로 채운 버퍼를
    // 여기서 해제할 때 다른 힙을 건드린다(호스트와 구성이 다르면 크래시).
    BindHeapAllocator(hostApi->Allocate, hostApi->Free);

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
