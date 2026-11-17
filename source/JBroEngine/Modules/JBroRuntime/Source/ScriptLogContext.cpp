#include <JBro/Runtime/ScriptLogContext.h>

#include <cstdio>

namespace JBro
{
    namespace
    {
        ScriptLogContext& BoundContext() noexcept
        {
            static ScriptLogContext context{0, 0, nullptr, nullptr};
            return context;
        }
    }

    ScriptContextBlock MakeScriptLogContextBlock(const ScriptLogContext& context) noexcept
    {
        return {ScriptLogContextTypeId, ScriptLogContextAbiVersion, static_cast<UInt32>(sizeof(ScriptLogContext)), &context};
    }

    const ScriptLogContext* FindScriptLogContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, ScriptLogContextTypeId);
        if (block == nullptr || block->AbiVersion != ScriptLogContextAbiVersion
            || block->Size != sizeof(ScriptLogContext) || block->Data == nullptr)
        {
            return nullptr;
        }
        const auto* found = static_cast<const ScriptLogContext*>(block->Data);
        return found->AbiVersion == ScriptLogContextAbiVersion && found->StructSize == sizeof(ScriptLogContext)
            && found->Write != nullptr ? found : nullptr;
    }

    void BindScriptLogContext(const ScriptLogContext& context) noexcept
    {
        BoundContext() = context;
    }

    void WriteEngineLog(LogLevel level, const char* category, const char* groupKey, const char* message) noexcept
    {
        const ScriptLogContext& bound = BoundContext();
        const char* safeCategory = category != nullptr ? category : "engine";
        const char* safeMessage = message != nullptr ? message : "";
        if (bound.AbiVersion == ScriptLogContextAbiVersion && bound.StructSize == sizeof(ScriptLogContext) && bound.Write != nullptr)
        {
            bound.Write(bound.Owner, static_cast<UInt32>(level), safeCategory, groupKey != nullptr ? groupKey : "", safeMessage);
            return;
        }
        try
        {
            Log::WriteGrouped(level, safeCategory, groupKey, "%s", safeMessage);
        }
        catch (...)
        {
            // 로그를 못 남겨도 부른 쪽(핸들의 안전 멤버)은 계속 돈다. 마지막으로 표준 에러에 남긴다.
            std::fprintf(stderr, "%s: %s\n", safeCategory, safeMessage);
        }
    }
}
