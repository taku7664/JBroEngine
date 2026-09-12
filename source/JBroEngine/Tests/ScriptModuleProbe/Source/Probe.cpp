#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>

#include <cstdint>

#ifndef JBRO_SCRIPT_PROBE_REVISION
#define JBRO_SCRIPT_PROBE_REVISION 1
#endif

namespace
{
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
        g_loaded = true;
        return true;
    }

    void UnloadModule() noexcept
    {
        JBro::BindFramework2DServiceContext({});
        JBro::BindFramework2DSystemContext({});
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

extern "C" __declspec(dllexport) std::uint32_t JBroScriptProbe_GetRevision() noexcept
{
    return JBRO_SCRIPT_PROBE_REVISION;
}
