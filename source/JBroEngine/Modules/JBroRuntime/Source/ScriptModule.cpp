#include <JBro/Runtime/ScriptModule.h>

namespace JBro
{
    const ScriptContextBlock* FindScriptContextBlock(
        const ScriptModuleLoadContext& context,
        ScriptContextTypeId typeId) noexcept
    {
        if (typeId == 0 || context.Extensions == nullptr)
        {
            return nullptr;
        }
        for (std::uint32_t index = 0; index < context.ExtensionCount; ++index)
        {
            if (context.Extensions[index].TypeId == typeId)
            {
                return context.Extensions + index;
            }
        }
        return nullptr;
    }

    bool ValidateScriptModuleLoadContext(
        const ScriptModuleLoadContext& context) noexcept
    {
        if (context.AbiVersion != ScriptModuleLoadContextAbiVersion
            || context.StructSize != sizeof(ScriptModuleLoadContext)
            || context.Systems == nullptr
            || context.Services == nullptr
            || context.Registry == nullptr
            || context.Names == nullptr
            || context.Systems->AbiVersion != SystemContextAbiVersion
            || context.Services->AbiVersion != ServiceContextAbiVersion
            || context.ExtensionCount > MaxScriptContextBlocks
            || (context.ExtensionCount != 0 && context.Extensions == nullptr))
        {
            return false;
        }

        for (std::uint32_t index = 0; index < context.ExtensionCount; ++index)
        {
            const ScriptContextBlock& block = context.Extensions[index];
            if (block.TypeId == 0 || block.AbiVersion == 0
                || block.Size == 0 || block.Data == nullptr)
            {
                return false;
            }
            for (std::uint32_t earlier = 0; earlier < index; ++earlier)
            {
                if (context.Extensions[earlier].TypeId == block.TypeId)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool BindScriptModuleContexts(
        const ScriptModuleLoadContext& context) noexcept
    {
        if (false == ValidateScriptModuleLoadContext(context))
        {
            return false;
        }
        BindSystemContext(*context.Systems);
        BindServiceContext(*context.Services);
        Internal::InstanceRegistry::Bind(context.Registry);
        NameTable::Bind(context.Names);
        return true;
    }
}
