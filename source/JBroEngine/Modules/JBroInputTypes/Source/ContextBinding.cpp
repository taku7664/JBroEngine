#include <JBro/InputTypes/Internal/ScriptModuleContext.h>
#include <JBro/InputTypes/Internal/SystemContext.h>
#include <JBro/InputTypes/ServiceContext.h>

namespace JBro
{
    namespace
    {
        // 이 모듈 사본의 접근점이다. 호스트와 스크립트 DLL 이 각자 하나씩 갖고, DLL 은 로드 때 호스트의 값을 받는다.
        InputSystemContext g_systemContext;
        InputServiceContext g_serviceContext;
    }

    void BindInputSystemContext(const InputSystemContext& context)
    {
        g_systemContext = context;
    }

    const InputSystemContext& GetInputSystems()
    {
        return g_systemContext;
    }

    void BindInputServiceContext(const InputServiceContext& context)
    {
        g_serviceContext = context;
    }

    const InputServiceContext& GetInputServices()
    {
        return g_serviceContext;
    }

    ScriptContextBlock MakeInputServiceContextBlock(const InputServiceContext& context) noexcept
    {
        return { InputServiceContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const InputServiceContext* FindInputServiceContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, InputServiceContextTypeId);
        if (nullptr == block || block->AbiVersion != InputServiceContextAbiVersion
            || block->Size != sizeof(InputServiceContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* services = static_cast<const InputServiceContext*>(block->Data);
        if (services->AbiVersion != InputServiceContextAbiVersion)
        {
            return nullptr;
        }
        return services;
    }

    ScriptContextBlock MakeInputSystemContextBlock(const InputSystemContext& context) noexcept
    {
        return { InputSystemContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const InputSystemContext* FindInputSystemContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, InputSystemContextTypeId);
        if (nullptr == block || block->AbiVersion != InputSystemContextAbiVersion
            || block->Size != sizeof(InputSystemContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* systems = static_cast<const InputSystemContext*>(block->Data);
        if (systems->AbiVersion != InputSystemContextAbiVersion)
        {
            return nullptr;
        }
        return systems;
    }
}
