#pragma once

#include <JBro/Platform/Platform.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Types/String.h>

#include <cstdint>

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
        bool Load(
            const char* dllPath,
            IPlatform& platform,
            const ScriptContextBlock* extensions = nullptr,
            std::uint32_t extensionCount = 0);
        void Unload(IPlatform& platform) noexcept;
        bool Reload(
            IPlatform& platform,
            const ScriptContextBlock* extensions = nullptr,
            std::uint32_t extensionCount = 0);

        void* GetSymbol(const char* name) const noexcept;
        bool IsLoaded() const noexcept;
        std::uint64_t GetGeneration() const noexcept;
        const String& GetLoadedPath() const noexcept;

    private:
        bool Activate(
            const char* dllPath,
            IPlatform& platform,
            const ScriptContextBlock* extensions,
            std::uint32_t extensionCount);
        void Deactivate(IPlatform& platform) noexcept;
        void AdvanceGeneration() noexcept;

        IPlatform* m_platform = nullptr;
        DynamicLibrary m_library;
        const ScriptModuleApi* m_api = nullptr;
        String m_path;
        std::uint64_t m_generation = 0;
    };
}
