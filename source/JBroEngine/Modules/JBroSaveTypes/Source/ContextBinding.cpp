#include <JBro/SaveTypes/Internal/ScriptModuleContext.h>
#include <JBro/SaveTypes/Internal/SystemContext.h>
#include <JBro/SaveTypes/ServiceContext.h>

namespace JBro
{
    namespace
    {
        // 이 모듈 사본의 접근점이다. 호스트와 스크립트 DLL 이 각자 하나씩 갖고, DLL 은 로드 때 호스트의 값을 받는다.
        SaveSystemContext g_systemContext;
        SaveServiceContext g_serviceContext;
    }

    void BindSaveSystemContext(const SaveSystemContext& context)
    {
        g_systemContext = context;
    }

    const SaveSystemContext& GetSaveSystems()
    {
        return g_systemContext;
    }

    void BindSaveServiceContext(const SaveServiceContext& context)
    {
        g_serviceContext = context;
    }

    const SaveServiceContext& GetSaveServices()
    {
        return g_serviceContext;
    }

    ScriptContextBlock MakeSaveServiceContextBlock(const SaveServiceContext& context) noexcept
    {
        return { SaveServiceContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const SaveServiceContext* FindSaveServiceContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, SaveServiceContextTypeId);
        if (nullptr == block || block->AbiVersion != SaveServiceContextAbiVersion
            || block->Size != sizeof(SaveServiceContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* services = static_cast<const SaveServiceContext*>(block->Data);
        if (services->AbiVersion != SaveServiceContextAbiVersion)
        {
            return nullptr;
        }
        return services;
    }

    ScriptContextBlock MakeSaveSystemContextBlock(const SaveSystemContext& context) noexcept
    {
        return { SaveSystemContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const SaveSystemContext* FindSaveSystemContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, SaveSystemContextTypeId);
        if (nullptr == block || block->AbiVersion != SaveSystemContextAbiVersion
            || block->Size != sizeof(SaveSystemContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* systems = static_cast<const SaveSystemContext*>(block->Data);
        if (systems->AbiVersion != SaveSystemContextAbiVersion)
        {
            return nullptr;
        }
        return systems;
    }
}
