#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/InputTypes/Internal/ScriptModuleContext.h>
#include <JBro/SaveTypes/Internal/ScriptModuleContext.h>
#include <JBro/LocalizationTypes/Internal/ScriptModuleContext.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/NameTable.h>

#include <cstdint>

#ifndef JBRO_SCRIPT_PROBE_REVISION
#define JBRO_SCRIPT_PROBE_REVISION 1
#endif

namespace
{
// 호스트가 이름으로 만들 수 있는 스크립트다. 이 타입은 DLL 안에만 있고
// 호스트는 그 정의를 보지 못한다 — 그게 이 경로의 요점이다(H5).
    class ProbeRegisteredScript final : public JBro::GameScript2D
{
public:
    static constexpr const char* StaticTypeName()
    {
        return "Probe::RegisteredScript";
    }

    JBro::ComponentTypeId GetTypeId() const override
    {
        return JBro::MakeStableTypeId(StaticTypeName());
    }

    void OnStart() override
    {
        m_started = true;
    }

    bool m_started = false;
    // 호스트 쪽 슬롯 크기가 DLL 쪽 sizeof 와 맞는지 보려고 일부러 채운다.
    double m_padding[4] = {1.0, 2.0, 3.0, 4.0};
};

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetScriptRegistry() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::ScriptRegistry::Get());
}

extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_GetRegisteredScriptSize() noexcept
{
    return static_cast<std::uint32_t>(sizeof(ProbeRegisteredScript));
}


    bool g_loaded = false;

    bool LoadModule(const JBro::ScriptModuleLoadContext* context) noexcept
    {
        if (context == nullptr
            || false == JBro::ValidateScriptModuleLoadContext(*context))
        {
            return false;
        }
        const JBro::Framework2DServiceContext* frameworkServices =
            JBro::FindFramework2DServiceContext(*context);
        const JBro::Framework2DSystemContext* frameworkSystems =
            JBro::FindFramework2DSystemContext(*context);
        if (frameworkServices == nullptr || frameworkSystems == nullptr)
        {
            return false;
        }
        if (false == JBro::BindScriptModuleContexts(*context))
        {
            return false;
        }
        JBro::BindFramework2DServiceContext(*frameworkServices);
        JBro::BindFramework2DSystemContext(*frameworkSystems);
        // 입력 블록은 호스트(EngineInstance)가 낸다(D-214). 블록만 손으로 건네는 로더 테스트에는 없으므로 있을 때만 묶는다.
        if (const JBro::InputServiceContext* inputServices = JBro::FindInputServiceContext(*context))
        {
            JBro::BindInputServiceContext(*inputServices);
        }
        if (const JBro::InputSystemContext* inputSystems = JBro::FindInputSystemContext(*context))
        {
            JBro::BindInputSystemContext(*inputSystems);
        }
        // 세이브 블록도 호스트가 낸다(D-218).
        if (const JBro::SaveServiceContext* saveServices = JBro::FindSaveServiceContext(*context))
        {
            JBro::BindSaveServiceContext(*saveServices);
        }
        if (const JBro::SaveSystemContext* saveSystems = JBro::FindSaveSystemContext(*context))
        {
            JBro::BindSaveSystemContext(*saveSystems);
        }
        // 문자열 표도 호스트가 낸다(D-226).
        if (const JBro::LocalizationServiceContext* localizationServices = JBro::FindLocalizationServiceContext(*context))
        {
            JBro::BindLocalizationServiceContext(*localizationServices);
        }
        if (const JBro::LocalizationSystemContext* localizationSystems = JBro::FindLocalizationSystemContext(*context))
        {
            JBro::BindLocalizationSystemContext(*localizationSystems);
        }
        // 이름으로 만들 수 있게 타입을 호스트 표에 등록한다. 여기서 만들어지는
        // 생성·파괴 함수는 이 DLL 안의 코드이며, 호스트는 그 주소만 부른다.
        if (false == JBro::RegisterScriptType2D<ProbeRegisteredScript>())
        {
            return false;
        }
        g_loaded = true;
        return true;
    }

    void UnloadModule() noexcept
    {
        JBro::BindFramework2DServiceContext({});
        JBro::BindFramework2DSystemContext({});
        JBro::BindInputServiceContext({});
        JBro::BindInputSystemContext({});
        JBro::BindSaveServiceContext({});
        JBro::BindSaveSystemContext({});
        JBro::BindLocalizationServiceContext({});
        JBro::BindLocalizationSystemContext({});
        JBro::Internal::InstanceRegistry::Bind(nullptr);
        JBro::ScriptRegistry::Bind(nullptr);
        JBro::NameTable::Bind(nullptr);
        JBro::TextStore::Bind(nullptr);
        JBro::BindSystemContext({});
        JBro::BindServiceContext({});
        g_loaded = false;
    }

    constexpr JBro::ScriptContextRequirement RequiredContexts[] =
    {
        JBro::Framework2DServiceContextRequirement,
        JBro::Framework2DSystemContextRequirement
    };

    constexpr JBro::ScriptModuleApi ModuleApi =
    {
        JBro::ScriptModuleAbiVersion,
        sizeof(JBro::ScriptModuleApi),
        RequiredContexts,
        2,
        0,
        &LoadModule,
        &UnloadModule
    };
}

extern "C" __declspec(dllexport) const JBro::ScriptModuleApi* JBroScriptModule_GetApi(
    std::uint32_t hostAbiVersion,
    std::uint32_t hostApiSize) noexcept
{
    if (hostAbiVersion != JBro::ScriptModuleAbiVersion
        || hostApiSize != sizeof(JBro::ScriptModuleApi))
    {
        return nullptr;
    }
    return &ModuleApi;
}

extern "C" __declspec(dllexport) bool JBroScriptProbe_IsLoaded() noexcept
{
    return g_loaded;
}

extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_GetSystemAbi() noexcept
{
    return JBro::GetSystemContext().AbiVersion;
}

extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_GetServiceAbi() noexcept
{
    return JBro::GetServiceContext().AbiVersion;
}

extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_GetFramework2DAbi() noexcept
{
    return JBro::GetFramework2DServices().AbiVersion;
}

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetPhysicsSystem() noexcept
{
    return reinterpret_cast<std::uintptr_t>(JBro::GetFramework2DSystems().Physics2D);
}

// 호스트가 넘긴 레지스트리에 실제로 붙었는지 본다. 붙지 않았다면 이 DLL 사본 주소가 나온다.
extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetRegistry() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::Internal::InstanceRegistry::Get());
}

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetLocalRegistry() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::Internal::InstanceRegistry::Local());
}

// 이름표도 같은 함정이 있다. 붙지 않았다면 이 DLL 사본 주소가 나오고,
// 호스트가 지은 태그의 원문을 이 안에서는 되찾지 못한다(D-51).
extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetNameTable() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::NameTable::Get());
}

// 글자 저장소도 같다(D-211). 붙지 않았다면 DLL 사본의 빈 저장소가 나온다.
extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetTextStore() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::TextStore::Get());
}

// 호스트가 만든 글자를 **DLL 쪽 코덱으로** 읽는다. 스크립트 타입의 `TextId` 필드가 파일에 적힐 때 지나는 길이다.
extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_WriteText(
    std::uint32_t index, std::uint32_t generation, char* buffer, std::uint32_t capacity) noexcept
{
    JBro::TextId id;
    id.index = index;
    id.generation = generation;
    std::size_t required = 0;
    if (false == JBro::GetTextIdCodec().ToText(&id, buffer, capacity, required))
    {
        return 0;
    }
    return static_cast<std::uint32_t>(required);
}

// 호스트가 이미 보관한 원문을 DLL 안에서 되찾을 수 있는지 직접 본다.
extern "C" __declspec(dllexport) const char* JBroScriptProbe_ResolveName(std::uint64_t id) noexcept
{
    return JBro::NameTable::Get().Resolve(id);
}

extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_GetRevision() noexcept
{
    return JBRO_SCRIPT_PROBE_REVISION;
}

// DLL 안의 스크립트가 서비스로 읽는 키보드다(D-214). 호스트가 접은 이번 프레임의 입력이 이 DLL 사본에 닿는지 본다.
extern "C" __declspec(dllexport) bool JBroScriptProbe_IsKeyDown(std::uint16_t key) noexcept
{
    return JBro::GetInputServices().Input.Keyboard().IsDown(static_cast<JBro::Key>(key));
}

// 시간과 난수가 DLL 까지 닿는다(D-242). 공통 컨텍스트는 `BindScriptModuleContexts` 가 묶는다 - 확장 블록이 필요 없다.
extern "C" __declspec(dllexport) float JBroScriptProbe_GetDeltaTime() noexcept
{
    return JBro::GetServiceContext().Time.DeltaTime();
}

extern "C" __declspec(dllexport) std::uint64_t JBroScriptProbe_GetFrameCount() noexcept
{
    return JBro::GetServiceContext().Time.FrameCount();
}

extern "C" __declspec(dllexport) void JBroScriptProbe_SetRandomSeed(std::uint64_t seed) noexcept
{
    JBro::GetServiceContext().Random.SetSeed(seed);
}

extern "C" __declspec(dllexport) std::int32_t JBroScriptProbe_RandomRange(std::int32_t min, std::int32_t max) noexcept
{
    return JBro::GetServiceContext().Random.Range(min, max);
}

// 디버그 선이 DLL 에서 호스트의 저장소로 간다(D-243). 서비스는 공통 시스템 컨텍스트의 저장소에 쌓는다.
extern "C" __declspec(dllexport) void JBroScriptProbe_DrawLine() noexcept
{
    JBro::GetFramework2DServices().DebugDraw.Line({0.0f, 0.0f}, {1.0f, 0.0f});
}

// DLL 안의 스크립트가 세이브를 쓰고 되읽는다(D-218). 읽은 바이트는 이 DLL 의 힙에 놓인다 - 호스트가 DLL 의 컨테이너를 키우지 않는지 본다.
extern "C" __declspec(dllexport) bool JBroScriptProbe_SaveRoundTrip(const char* slot, const char* text) noexcept
{
    const JBro::Service::SaveService& save = JBro::GetSaveServices().Save;
    const JBro::String written(text);
    if (false == save.WriteText(slot, written))
    {
        return false;
    }
    JBro::String read;
    return save.ReadText(slot, read) && read == written;
}

// DLL 안의 스크립트가 로케일을 바꾸고 키의 글자를 제 힙에 받는다(D-226). 돌려주는 것은 글자 길이이고, 로케일을 못 바꾸면 0 이다.
extern "C" __declspec(dllexport) std::size_t JBroScriptProbe_Localize(const char* locale, const char* key, char* buffer,
    std::size_t capacity) noexcept
{
    const JBro::Service::LocalizationService& localization = JBro::GetLocalizationServices().Localization;
    if (false == localization.SetLocale(locale) || localization.GetLocale() != locale)
    {
        return 0;
    }
    const JBro::String text = localization.GetText(key);
    if (text.size() > capacity)
    {
        return 0;
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        buffer[index] = text[index];
    }
    return text.size();
}

// DLL 안의 스크립트가 리바인딩을 글자로 받는다(D-218). 서비스가 크기를 묻고 제 힙에 버퍼를 키운다.
extern "C" __declspec(dllexport) std::size_t JBroScriptProbe_WriteBindingOverrides(char* buffer, std::size_t capacity) noexcept
{
    JBro::String text;
    if (false == JBro::GetInputServices().Input.WriteBindingOverrides(text) || text.size() > capacity)
    {
        return 0;
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        buffer[index] = text[index];
    }
    return text.size();
}
