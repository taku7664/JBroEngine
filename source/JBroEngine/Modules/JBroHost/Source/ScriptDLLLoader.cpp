#include <JBro/Host/ScriptDLLLoader.h>

#include <limits>

namespace JBro
{
    namespace
    {
        bool ValidateModuleApi(
            const ScriptModuleApi* api,
            const ScriptModuleLoadContext& context) noexcept
        {
            if (api == nullptr
                || api->AbiVersion != ScriptModuleAbiVersion
                || api->StructSize != sizeof(ScriptModuleApi)
                || api->Load == nullptr
                || api->Unload == nullptr
                || api->RequiredContextCount > MaxScriptContextBlocks
                || (api->RequiredContextCount != 0 && api->RequiredContexts == nullptr))
            {
                return false;
            }

            for (std::uint32_t index = 0; index < api->RequiredContextCount; ++index)
            {
                const ScriptContextRequirement& requirement = api->RequiredContexts[index];
                if (requirement.TypeId == 0 || requirement.AbiVersion == 0
                    || requirement.Size == 0)
                {
                    return false;
                }
                for (std::uint32_t earlier = 0; earlier < index; ++earlier)
                {
                    if (api->RequiredContexts[earlier].TypeId == requirement.TypeId)
                    {
                        return false;
                    }
                }

                const ScriptContextBlock* block =
                    FindScriptContextBlock(context, requirement.TypeId);
                if (block == nullptr
                    || block->AbiVersion != requirement.AbiVersion
                    || block->Size != requirement.Size)
                {
                    return false;
                }
            }
            return true;
        }

        // DLL 이 등록한 것을 거둔다. 둘 다 그 DLL 안의 함수 포인터를 든다 - 코드가 사라지기 전에 부른다.
        // **로드가 실패한 길에서도 부른다.** `Load` 가 타입 몇 개를 등록한 뒤 실패하면 그 항목이 사라진 코드를 가리킨 채 남는다.
        void ClearModuleTables() noexcept
        {
            ScriptRegistry::Local().Clear();
            PropertyRegistry::ScriptLocal().Clear();
        }
    }

    ScriptDLLLoader::~ScriptDLLLoader()
    {
        if (m_platform != nullptr)
        {
            Unload(*m_platform);
        }
    }

    bool ScriptDLLLoader::Load(
        const char* dllPath,
        IPlatform& platform,
        const ScriptContextBlock* extensions,
        std::uint32_t extensionCount)
    {
        if (IsLoaded())
        {
            return false;
        }
        if (false == Activate(dllPath, platform, extensions, extensionCount))
        {
            return false;
        }
        AdvanceGeneration();
        return true;
    }

    void ScriptDLLLoader::Unload(IPlatform& platform) noexcept
    {
        if (false == IsLoaded())
        {
            return;
        }
        Deactivate(platform);
        m_path.Clear(false);
        AdvanceGeneration();
    }

    bool ScriptDLLLoader::Reload(
        IPlatform& platform,
        const ScriptContextBlock* extensions,
        std::uint32_t extensionCount)
    {
        if (false == IsLoaded() || m_platform != &platform)
        {
            return false;
        }

        const String path = m_path;
        Deactivate(platform);
        m_path.Clear(false);
        const bool activated = Activate(
            path.c_str(), platform, extensions, extensionCount);
        AdvanceGeneration();
        return activated;
    }

    void* ScriptDLLLoader::GetSymbol(const char* name) const noexcept
    {
        if (false == IsLoaded() || name == nullptr || name[0] == '\0')
        {
            return nullptr;
        }
        return m_platform->GetSymbol(m_library, name);
    }

    bool ScriptDLLLoader::IsLoaded() const noexcept
    {
        return m_platform != nullptr && m_library.opaque != nullptr && m_api != nullptr;
    }

    std::uint64_t ScriptDLLLoader::GetGeneration() const noexcept
    {
        return m_generation;
    }

    const String& ScriptDLLLoader::GetLoadedPath() const noexcept
    {
        return m_path;
    }

    bool ScriptDLLLoader::Activate(
        const char* dllPath,
        IPlatform& platform,
        const ScriptContextBlock* extensions,
        std::uint32_t extensionCount)
    {
        if (dllPath == nullptr || dllPath[0] == '\0'
            || extensionCount > MaxScriptContextBlocks
            || (extensionCount != 0 && extensions == nullptr))
        {
            return false;
        }

        ScriptModuleLoadContext context;
        context.Systems = &GetSystemContext();
        context.Services = &GetServiceContext();
        context.Registry = &Internal::InstanceRegistry::Local();
        context.Names = &NameTable::Local();
        context.Scripts = &ScriptRegistry::Local();
        context.Texts = &TextStore::Local();
        context.Properties = &PropertyRegistry::ScriptLocal();
        context.Extensions = extensions;
        context.ExtensionCount = extensionCount;
        if (false == ValidateScriptModuleLoadContext(context))
        {
            return false;
        }

        const DynamicLibrary library = platform.LoadDynamicLibrary(dllPath);
        if (library.opaque == nullptr)
        {
            return false;
        }

        const auto getApi = reinterpret_cast<GetScriptModuleApiFunction>(
            platform.GetSymbol(library, ScriptModuleEntryPointName));
        const ScriptModuleApi* api = nullptr;
        if (getApi != nullptr)
        {
            api = getApi(ScriptModuleAbiVersion, sizeof(ScriptModuleApi));
        }
        if (false == ValidateModuleApi(api, context))
        {
            platform.UnloadDynamicLibrary(library);
            return false;
        }
        if (false == api->Load(&context))
        {
            ClearModuleTables();
            api->Unload();
            platform.UnloadDynamicLibrary(library);
            return false;
        }

        try
        {
            m_path = dllPath;
        }
        catch (...)
        {
            ClearModuleTables();
            api->Unload();
            platform.UnloadDynamicLibrary(library);
            return false;
        }
        m_platform = &platform;
        m_library = library;
        m_api = api;
        return true;
    }

    void ScriptDLLLoader::Deactivate(IPlatform& platform) noexcept
    {
        IPlatform* activePlatform = m_platform;
        if (activePlatform == nullptr)
        {
            activePlatform = &platform;
        }
        const ScriptModuleApi* api = m_api;
        const DynamicLibrary library = m_library;
        // DLL 이 등록한 타입은 그 DLL 안의 함수 포인터다. 코드가 사라지기 전에 지운다.
        // 프로퍼티 표도 같다 - 접근자와 설명서가 모두 DLL 안에 있다(cpp-script-plan §3.1).
        ClearModuleTables();
        m_api = nullptr;
        m_library = {};
        m_platform = nullptr;

        if (api != nullptr)
        {
            api->Unload();
        }
        if (library.opaque != nullptr)
        {
            activePlatform->UnloadDynamicLibrary(library);
        }
    }

    void ScriptDLLLoader::AdvanceGeneration() noexcept
    {
        if (m_generation == std::numeric_limits<std::uint64_t>::max())
        {
            m_generation = 1;
            return;
        }
        ++m_generation;
    }
}
