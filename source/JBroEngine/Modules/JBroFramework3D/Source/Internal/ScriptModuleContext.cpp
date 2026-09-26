#include <JBro/Framework3D/Internal/ScriptModuleContext.h>

namespace JBro
{
    ScriptContextBlock MakeFramework3DServiceContextBlock(
        const Framework3DServiceContext& context) noexcept
    {
        return {
            Framework3DServiceContextTypeId,
            context.AbiVersion,
            static_cast<std::uint32_t>(sizeof(context)),
            &context};
    }

    const Framework3DServiceContext* FindFramework3DServiceContext(
        const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block =
            FindScriptContextBlock(context, Framework3DServiceContextTypeId);
        if (block == nullptr
            || block->AbiVersion != Framework3DServiceContextAbiVersion
            || block->Size != sizeof(Framework3DServiceContext)
            || block->Data == nullptr)
        {
            return nullptr;
        }

        const auto* serviceContext =
            static_cast<const Framework3DServiceContext*>(block->Data);
        if (serviceContext->AbiVersion != Framework3DServiceContextAbiVersion)
        {
            return nullptr;
        }
        return serviceContext;
    }

    ScriptContextBlock MakeFramework3DSystemContextBlock(
        const Framework3DSystemContext& context) noexcept
    {
        return {
            Framework3DSystemContextTypeId,
            context.AbiVersion,
            static_cast<std::uint32_t>(sizeof(context)),
            &context};
    }

    const Framework3DSystemContext* FindFramework3DSystemContext(
        const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block =
            FindScriptContextBlock(context, Framework3DSystemContextTypeId);
        if (block == nullptr
            || block->AbiVersion != Framework3DSystemContextAbiVersion
            || block->Size != sizeof(Framework3DSystemContext)
            || block->Data == nullptr)
        {
            return nullptr;
        }

        const auto* systemContext =
            static_cast<const Framework3DSystemContext*>(block->Data);
        if (systemContext->AbiVersion != Framework3DSystemContextAbiVersion)
        {
            return nullptr;
        }
        return systemContext;
    }
}
