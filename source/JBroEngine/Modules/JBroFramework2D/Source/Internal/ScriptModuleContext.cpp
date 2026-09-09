#include <JBro/Framework2D/Internal/ScriptModuleContext.h>

namespace JBro
{
    ScriptContextBlock MakeFramework2DServiceContextBlock(
        const Framework2DServiceContext& context) noexcept
    {
        return {
            Framework2DServiceContextTypeId,
            context.AbiVersion,
            static_cast<std::uint32_t>(sizeof(context)),
            &context};
    }

    const Framework2DServiceContext* FindFramework2DServiceContext(
        const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block =
            FindScriptContextBlock(context, Framework2DServiceContextTypeId);
        if (block == nullptr
            || block->AbiVersion != Framework2DServiceContextAbiVersion
            || block->Size != sizeof(Framework2DServiceContext)
            || block->Data == nullptr)
        {
            return nullptr;
        }

        const auto* serviceContext =
            static_cast<const Framework2DServiceContext*>(block->Data);
        if (serviceContext->AbiVersion != Framework2DServiceContextAbiVersion)
        {
            return nullptr;
        }
        return serviceContext;
    }
}
