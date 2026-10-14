#include <JBro/Runtime/ScriptErrorContext.h>

namespace JBro
{
    namespace
    {
        ScriptErrorContext& BoundContext() noexcept
        {
            static ScriptErrorContext context;
            return context;
        }
    }

    ScriptContextBlock MakeScriptErrorContextBlock(const ScriptErrorContext& context) noexcept
    {
        return {ScriptErrorContextTypeId, ScriptErrorContextAbiVersion,
            static_cast<UInt32>(sizeof(ScriptErrorContext)), &context};
    }

    const ScriptErrorContext* FindScriptErrorContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, ScriptErrorContextTypeId);
        if (block == nullptr || block->AbiVersion != ScriptErrorContextAbiVersion
            || block->Size != sizeof(ScriptErrorContext) || block->Data == nullptr)
        {
            return nullptr;
        }
        const auto* found = static_cast<const ScriptErrorContext*>(block->Data);
        return found->AbiVersion == ScriptErrorContextAbiVersion
            && found->StructSize == sizeof(ScriptErrorContext) && found->Report != nullptr
            ? found : nullptr;
    }

    void BindScriptErrorContext(const ScriptErrorContext& context) noexcept
    {
        BoundContext() = context;
    }

    void ReportScriptError(const char* siteKey, const char* message) noexcept
    {
        const ScriptErrorContext& context = BoundContext();
        if (context.AbiVersion == ScriptErrorContextAbiVersion
            && context.StructSize == sizeof(ScriptErrorContext) && context.Report != nullptr)
        {
            context.Report(context.Owner, context.ExpectedGeneration,
                siteKey != nullptr ? siteKey : "", message != nullptr ? message : "invalid ref access");
        }
    }
}
