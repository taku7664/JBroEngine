#pragma once

#include <JBro/Platform/Platform.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class ScriptDLLLoader final
    {
    public:
        ScriptDLLLoader() = default;
        ~ScriptDLLLoader();
        ScriptDLLLoader(const ScriptDLLLoader&) = delete;
        ScriptDLLLoader& operator=(const ScriptDLLLoader&) = delete;
        ScriptDLLLoader(ScriptDLLLoader&&) = delete;
        ScriptDLLLoader& operator=(ScriptDLLLoader&&) = delete;

        // Main-thread only. The platform must outlive an active loader.
        Bool Load(
            const char* dllPath,
            IPlatform& platform,
            const ScriptContextBlock* extensions = nullptr,
            UInt32 extensionCount = 0);
        void Unload(IPlatform& platform) noexcept;
        Bool Reload(
            IPlatform& platform,
            const ScriptContextBlock* extensions = nullptr,
            UInt32 extensionCount = 0);

        void* GetSymbol(const char* name) const noexcept;
        Bool IsLoaded() const noexcept;
        UInt64 GetGeneration() const noexcept;
        const String& GetLoadedPath() const noexcept;

    private:
        Bool Activate(
            const char* dllPath,
            IPlatform& platform,
            const ScriptContextBlock* extensions,
            UInt32 extensionCount);
        void Deactivate(IPlatform& platform) noexcept;
        void AdvanceGeneration() noexcept;

        IPlatform* m_platform = nullptr;
        DynamicLibrary m_library;
        const ScriptModuleApi* m_api = nullptr;
        String m_path;
        UInt64 m_generation = 0;
    };
}
