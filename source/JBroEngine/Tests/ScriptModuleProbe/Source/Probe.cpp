#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>

#include <cstdint>

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
        if (frameworkServices == nullptr)
        {
            return false;
        }
        if (false == JBro::BindScriptModuleContexts(*context))
        {
            return false;
        }
        JBro::BindFramework2DServiceContext(*frameworkServices);
        g_loaded = true;
        return true;
    }

    void UnloadModule() noexcept
    {
        JBro::BindFramework2DServiceContext({});
        JBro::BindSystemContext({});
        JBro::BindServiceContext({});
        g_loaded = false;
    }

    constexpr JBro::ScriptContextRequirement RequiredContexts[] =
    {
        JBro::Framework2DServiceContextRequirement
    };

    constexpr JBro::ScriptModuleApi ModuleApi =
    {
        JBro::ScriptModuleAbiVersion,
        sizeof(JBro::ScriptModuleApi),
        RequiredContexts,
        1,
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
    return reinterpret_cast<std::uintptr_t>(JBro::GetSystemContext().Physics2D);
}
