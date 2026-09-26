#include <JBro/LocalizationTypes/Internal/ScriptModuleContext.h>
#include <JBro/LocalizationTypes/Internal/SystemContext.h>
#include <JBro/LocalizationTypes/ServiceContext.h>

namespace JBro
{
    namespace
    {
        // 이 모듈 사본의 접근점이다. 호스트와 스크립트 DLL 이 각자 하나씩 갖고, DLL 은 로드 때 호스트의 값을 받는다.
        LocalizationSystemContext g_systemContext;
        LocalizationServiceContext g_serviceContext;
    }

    void BindLocalizationSystemContext(const LocalizationSystemContext& context)
    {
        g_systemContext = context;
    }

    const LocalizationSystemContext& GetLocalizationSystems()
    {
        return g_systemContext;
    }

    void BindLocalizationServiceContext(const LocalizationServiceContext& context)
    {
        g_serviceContext = context;
    }

    const LocalizationServiceContext& GetLocalizationServices()
    {
        return g_serviceContext;
    }

    ScriptContextBlock MakeLocalizationServiceContextBlock(const LocalizationServiceContext& context) noexcept
    {
        return { LocalizationServiceContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const LocalizationServiceContext* FindLocalizationServiceContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, LocalizationServiceContextTypeId);
        if (nullptr == block || block->AbiVersion != LocalizationServiceContextAbiVersion
            || block->Size != sizeof(LocalizationServiceContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* services = static_cast<const LocalizationServiceContext*>(block->Data);
        if (services->AbiVersion != LocalizationServiceContextAbiVersion)
        {
            return nullptr;
        }
        return services;
    }

    ScriptContextBlock MakeLocalizationSystemContextBlock(const LocalizationSystemContext& context) noexcept
    {
        return { LocalizationSystemContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const LocalizationSystemContext* FindLocalizationSystemContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, LocalizationSystemContextTypeId);
        if (nullptr == block || block->AbiVersion != LocalizationSystemContextAbiVersion
            || block->Size != sizeof(LocalizationSystemContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* systems = static_cast<const LocalizationSystemContext*>(block->Data);
        if (systems->AbiVersion != LocalizationSystemContextAbiVersion)
        {
            return nullptr;
        }
        return systems;
    }
}
