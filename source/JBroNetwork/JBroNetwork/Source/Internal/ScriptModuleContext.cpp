#include <JBro/Network/Internal/ScriptModuleContext.h>

namespace JBro
{
    ScriptContextBlock MakeNetworkServiceContextBlock(const NetworkServiceContext& context) noexcept
    {
        return { NetworkServiceContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const NetworkServiceContext* FindNetworkServiceContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, NetworkServiceContextTypeId);
        if (nullptr == block || block->AbiVersion != NetworkServiceContextAbiVersion
            || block->Size != sizeof(NetworkServiceContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* services = static_cast<const NetworkServiceContext*>(block->Data);
        if (services->AbiVersion != NetworkServiceContextAbiVersion)
        {
            return nullptr;
        }
        return services;
    }

    ScriptContextBlock MakeNetworkSystemContextBlock(const NetworkSystemContext& context) noexcept
    {
        return { NetworkSystemContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context };
    }

    const NetworkSystemContext* FindNetworkSystemContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, NetworkSystemContextTypeId);
        if (nullptr == block || block->AbiVersion != NetworkSystemContextAbiVersion
            || block->Size != sizeof(NetworkSystemContext) || nullptr == block->Data)
        {
            return nullptr;
        }
        const auto* systems = static_cast<const NetworkSystemContext*>(block->Data);
        if (systems->AbiVersion != NetworkSystemContextAbiVersion)
        {
            return nullptr;
        }
        return systems;
    }
}
