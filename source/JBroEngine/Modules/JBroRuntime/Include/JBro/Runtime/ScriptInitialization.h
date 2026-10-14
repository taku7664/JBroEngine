#pragma once

#include <JBro/Runtime/ScriptGuard.h>

#include <cstddef>
#include <iterator>
#include <utility>

namespace JBro::ScriptGuard
{
    namespace Internal
    {
        struct InitializingObject
        {
            const void* Owner = nullptr;
            ScriptContextTypeId Type = 0;
            std::size_t Completed = 0;
        };

        // Only constructors in progress are tracked. Ordinary objects keep no
        // extra state, and field access never allocates or builds a message.
        inline thread_local InitializingObject InitializingObjects[256]{};
        inline thread_local std::size_t InitializingCount = 0;

        inline InitializingObject* FindInitializingObject(const void* owner, ScriptContextTypeId type) noexcept
        {
            for (std::size_t at = InitializingCount; at > 0; --at)
            {
                InitializingObject& object = InitializingObjects[at - 1];
                if (object.Owner == owner && object.Type == type)
                {
                    return &object;
                }
            }
            return nullptr;
        }
    }

    inline void BeginFieldInitialization(const void* owner, ScriptContextTypeId type, std::size_t ordinal,
        const char* siteKey, const char* message)
    {
        if (ordinal == 0)
        {
            if (Internal::InitializingCount == std::size(Internal::InitializingObjects))
            {
                ReportScriptError(siteKey, message);
                throw AbortCallback{};
            }
            Internal::InitializingObjects[Internal::InitializingCount++] = {owner, type, 0};
        }
        else if (Internal::InitializingObject* object = Internal::FindInitializingObject(owner, type))
        {
            // Reaching the next initializer proves the preceding field's
            // construction (including its move/copy) completed successfully.
            object->Completed = ordinal;
        }
    }

    inline void FinishInitialization(const void* owner, ScriptContextTypeId type) noexcept
    {
        for (std::size_t at = Internal::InitializingCount; at > 0; --at)
        {
            if (Internal::InitializingObjects[at - 1].Owner == owner
                && Internal::InitializingObjects[at - 1].Type == type)
            {
                for (std::size_t next = at; next < Internal::InitializingCount; ++next)
                {
                    Internal::InitializingObjects[next - 1] = Internal::InitializingObjects[next];
                }
                --Internal::InitializingCount;
                return;
            }
        }
    }

    inline void RequireInitializedField(const void* owner, ScriptContextTypeId type, std::size_t ordinal,
        const char* siteKey, const char* message)
    {
        const Internal::InitializingObject* object = Internal::FindInitializingObject(owner, type);
        if (object != nullptr && ordinal >= object->Completed)
        {
            ReportScriptError(siteKey, message);
            throw AbortCallback{};
        }
    }

    template<typename Owner, typename T>
    decltype(auto) RequireInitializedValue(T&& value, ScriptContextTypeId type,
        const char* siteKey, const char* message)
    {
        const void* owner = static_cast<const Owner*>(&value);
        if (Internal::FindInitializingObject(owner, type) != nullptr)
        {
            ReportScriptError(siteKey, message);
            throw AbortCallback{};
        }
        return std::forward<T>(value);
    }
}
