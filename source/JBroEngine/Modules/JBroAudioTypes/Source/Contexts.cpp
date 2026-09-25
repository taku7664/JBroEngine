#include <JBro/AudioTypes/Internal/ScriptModuleContext.h>

namespace JBro
{
    namespace
    {
        AudioServiceContext g_services;
        AudioSystemContext g_systems;
    }

    void BindAudioServiceContext(const AudioServiceContext& context)
    {
        g_services = context;
    }

    const AudioServiceContext& GetAudioServices()
    {
        return g_services;
    }

    void BindAudioSystemContext(const AudioSystemContext& context)
    {
        g_systems = context;
    }

    const AudioSystemContext& GetAudioSystems()
    {
        return g_systems;
    }

    ScriptContextBlock MakeAudioServiceContextBlock(const AudioServiceContext& context) noexcept
    {
        return {AudioServiceContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context};
    }

    const AudioServiceContext* FindAudioServiceContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, AudioServiceContextTypeId);
        if (block == nullptr || block->AbiVersion != AudioServiceContextAbiVersion
            || block->Size != sizeof(AudioServiceContext) || block->Data == nullptr)
        {
            return nullptr;
        }
        const auto* services = static_cast<const AudioServiceContext*>(block->Data);
        return services->AbiVersion == AudioServiceContextAbiVersion ? services : nullptr;
    }

    ScriptContextBlock MakeAudioSystemContextBlock(const AudioSystemContext& context) noexcept
    {
        return {AudioSystemContextTypeId, context.AbiVersion, static_cast<std::uint32_t>(sizeof(context)), &context};
    }

    const AudioSystemContext* FindAudioSystemContext(const ScriptModuleLoadContext& context) noexcept
    {
        const ScriptContextBlock* block = FindScriptContextBlock(context, AudioSystemContextTypeId);
        if (block == nullptr || block->AbiVersion != AudioSystemContextAbiVersion
            || block->Size != sizeof(AudioSystemContext) || block->Data == nullptr)
        {
            return nullptr;
        }
        const auto* systems = static_cast<const AudioSystemContext*>(block->Data);
        return systems->AbiVersion == AudioSystemContextAbiVersion ? systems : nullptr;
    }
}
