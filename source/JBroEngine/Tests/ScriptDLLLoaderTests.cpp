#include <JBro/Core/StableTypeId.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/ScriptDLLLoader.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>

#include <Windows.h>

#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    enum class Event : std::uint32_t
    {
        PlatformLoad,
        ModuleLoad,
        ModuleUnload,
        PlatformUnload
    };

    struct EventLog
    {
        Event values[16]{};
        std::uint32_t count = 0;

        void Add(Event event)
        {
            Check(count < 16, "script loader event log overflow");
            values[count] = event;
            ++count;
        }
    };

    struct ProbeExtension
    {
        std::uint32_t AbiVersion = 7;
        std::uint32_t value = 42;
    };

    constexpr JBro::ScriptContextTypeId ProbeContextTypeId =
        JBro::MakeStableTypeId("JBro.Tests.ProbeContext");

    struct ModuleProbe
    {
        EventLog* events = nullptr;
        bool loadResult = true;
        std::uint32_t loadCalls = 0;
        std::uint32_t unloadCalls = 0;
        JBro::SystemContext receivedSystems;
        JBro::ServiceContext receivedServices;
        ProbeExtension receivedExtension;
        bool receivedExtensionBlock = false;
    };

    ModuleProbe* g_moduleProbe = nullptr;
    JBro::ScriptContextRequirement g_requirements[1]{};
    JBro::ScriptModuleApi g_api{};

    bool LoadProbeModule(const JBro::ScriptModuleLoadContext* context) noexcept
    {
        if (g_moduleProbe == nullptr || context == nullptr)
        {
            return false;
        }
        g_moduleProbe->events->Add(Event::ModuleLoad);
        ++g_moduleProbe->loadCalls;
        g_moduleProbe->receivedSystems = *context->Systems;
        g_moduleProbe->receivedServices = *context->Services;
        const JBro::ScriptContextBlock* extension =
            JBro::FindScriptContextBlock(*context, ProbeContextTypeId);
        if (extension != nullptr)
        {
            g_moduleProbe->receivedExtension =
                *static_cast<const ProbeExtension*>(extension->Data);
            g_moduleProbe->receivedExtensionBlock = true;
        }
        return g_moduleProbe->loadResult;
    }

    void UnloadProbeModule() noexcept
    {
        if (g_moduleProbe == nullptr)
        {
            return;
        }
        g_moduleProbe->events->Add(Event::ModuleUnload);
        ++g_moduleProbe->unloadCalls;
    }

    const JBro::ScriptModuleApi* GetProbeApi(
        std::uint32_t,
        std::uint32_t) noexcept
    {
        return &g_api;
    }

    class LoaderPlatform final : public JBro::IPlatform
    {
    public:
        explicit LoaderPlatform(EventLog& events)
            : m_events(events)
        {
        }

        bool Initialize(const JBro::JMemoryContext&) override
        {
            return true;
        }

        void Shutdown() override
        {
        }

        JBro::WindowHandle OpenPlatformWindow(const JBro::WindowDesc&) override
        {
            return {};
        }

        void ClosePlatformWindow(JBro::WindowHandle) override
        {
        }

        JBro::SurfaceHandle CreateSurface(JBro::WindowHandle) override
        {
            return {};
        }

        void PumpEvents() override
        {
        }

        void WaitForEvents(std::uint32_t) override
        {
        }

        bool ShouldClose(JBro::WindowHandle) const override
        {
            return false;
        }

        bool GetWindowState(JBro::WindowHandle, JBro::WindowState&) const override
        {
            return false;
        }

        JBro::DynamicLibrary LoadDynamicLibrary(const char* utf8Path) override
        {
            m_events.Add(Event::PlatformLoad);
            ++loadCalls;
            if (utf8Path == nullptr || utf8Path[0] == '\0' || false == allowLoad)
            {
                return {};
            }
            return {reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1234))};
        }

        void* GetSymbol(JBro::DynamicLibrary library, const char* name) override
        {
            if (library.opaque == nullptr || name == nullptr)
            {
                return nullptr;
            }
            if (0 == std::strcmp(name, JBro::ScriptModuleEntryPointName))
            {
                if (false == exposeEntryPoint)
                {
                    return nullptr;
                }
                return reinterpret_cast<void*>(&GetProbeApi);
            }
            if (0 == std::strcmp(name, "ProbeSymbol"))
            {
                return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x5678));
            }
            return nullptr;
        }

        void UnloadDynamicLibrary(JBro::DynamicLibrary library) override
        {
            Check(library.opaque != nullptr, "platform must not unload a null DLL handle");
            m_events.Add(Event::PlatformUnload);
            ++unloadCalls;
        }

        bool allowLoad = true;
        bool exposeEntryPoint = true;
        std::uint32_t loadCalls = 0;
        std::uint32_t unloadCalls = 0;

    private:
        EventLog& m_events;
    };

    void ResetApi(ModuleProbe& probe)
    {
        g_moduleProbe = &probe;
        g_requirements[0] = {
            ProbeContextTypeId,
            7,
            static_cast<std::uint32_t>(sizeof(ProbeExtension))};
        g_api = {};
        g_api.AbiVersion = JBro::ScriptModuleAbiVersion;
        g_api.StructSize = sizeof(JBro::ScriptModuleApi);
        g_api.RequiredContexts = g_requirements;
        g_api.RequiredContextCount = 1;
        g_api.Load = &LoadProbeModule;
        g_api.Unload = &UnloadProbeModule;
    }

    JBro::ScriptContextBlock MakeProbeBlock(const ProbeExtension& extension)
    {
        return {
            ProbeContextTypeId,
            extension.AbiVersion,
            static_cast<std::uint32_t>(sizeof(extension)),
            &extension};
    }

    void BindCommonContexts()
    {
        JBro::SystemContext systems;
        JBro::ServiceContext services;
        JBro::BindSystemContext(systems);
        JBro::BindServiceContext(services);
    }

    void TestRejectsInvalidModuleAbiBeforeCallingModule()
    {
        EventLog events;
        ModuleProbe probe{&events};
        ResetApi(probe);
        g_api.AbiVersion = JBro::ScriptModuleAbiVersion + 1;
        LoaderPlatform platform(events);
        ProbeExtension extension;
        const JBro::ScriptContextBlock block = MakeProbeBlock(extension);
        BindCommonContexts();

        JBro::ScriptDLLLoader loader;
        Check(false == loader.Load("probe.dll", platform, &block, 1),
            "loader must reject a script module ABI mismatch");
        Check(probe.loadCalls == 0,
            "module Load must not run before its ABI is accepted");
        Check(platform.unloadCalls == 1 && false == loader.IsLoaded(),
            "rejected modules must release their dynamic library handle");
        Check(loader.GetGeneration() == 0,
            "a module that was never activated must not advance generation");
    }

    void TestRequiresExactUniqueContextBlocks()
    {
        EventLog events;
        ModuleProbe probe{&events};
        ResetApi(probe);
        LoaderPlatform platform(events);
        BindCommonContexts();
        ProbeExtension extension;

        JBro::ScriptDLLLoader loader;
        Check(false == loader.Load("probe.dll", platform, nullptr, 0),
            "loader must reject a missing required context");
        Check(probe.loadCalls == 0,
            "missing contexts must be rejected before module code runs");

        auto wrongVersion = MakeProbeBlock(extension);
        ++wrongVersion.AbiVersion;
        Check(false == loader.Load("probe.dll", platform, &wrongVersion, 1),
            "loader must reject an extension ABI mismatch");

        auto wrongSize = MakeProbeBlock(extension);
        --wrongSize.Size;
        Check(false == loader.Load("probe.dll", platform, &wrongSize, 1),
            "loader must reject an extension size mismatch");

        const JBro::ScriptContextBlock valid = MakeProbeBlock(extension);
        const JBro::ScriptContextBlock duplicates[] = {valid, valid};
        Check(false == loader.Load("probe.dll", platform, duplicates, 2),
            "loader must reject duplicate extension context ids");
        Check(probe.loadCalls == 0,
            "all context validation must finish before module code runs");
        Check(platform.loadCalls == platform.unloadCalls,
            "every rejected context candidate must release its DLL handle");
    }

    void TestRejectsCommonContextAbiBeforeOpeningDll()
    {
        EventLog events;
        ModuleProbe probe{&events};
        ResetApi(probe);
        LoaderPlatform platform(events);
        ProbeExtension extension;
        const JBro::ScriptContextBlock block = MakeProbeBlock(extension);
        JBro::SystemContext systems;
        ++systems.AbiVersion;
        JBro::BindSystemContext(systems);
        JBro::BindServiceContext({});

        JBro::ScriptDLLLoader loader;
        Check(false == loader.Load("probe.dll", platform, &block, 1),
            "loader must reject a common context ABI mismatch");
        Check(platform.loadCalls == 0 && probe.loadCalls == 0,
            "invalid host contexts must be rejected before the DLL is opened");
        BindCommonContexts();
    }

    void TestFailedModuleActivationRollsBack()
    {
        EventLog events;
        ModuleProbe probe{&events};
        ResetApi(probe);
        probe.loadResult = false;
        LoaderPlatform platform(events);
        BindCommonContexts();
        ProbeExtension extension;
        const JBro::ScriptContextBlock block = MakeProbeBlock(extension);

        JBro::ScriptDLLLoader loader;
        Check(false == loader.Load("probe.dll", platform, &block, 1),
            "module activation failure must fail the load transaction");
        Check(probe.loadCalls == 1 && probe.unloadCalls == 1,
            "failed activation must run the module rollback hook exactly once");
        Check(platform.unloadCalls == 1 && false == loader.IsLoaded(),
            "failed activation must release its candidate DLL");
        Check(events.count == 4
            && events.values[0] == Event::PlatformLoad
            && events.values[1] == Event::ModuleLoad
            && events.values[2] == Event::ModuleUnload
            && events.values[3] == Event::PlatformUnload,
            "activation rollback must precede candidate DLL release");
    }

    void TestLoadReloadAndUnloadOrder()
    {
        EventLog events;
        ModuleProbe probe{&events};
        ResetApi(probe);
        LoaderPlatform platform(events);
        BindCommonContexts();
        ProbeExtension extension;
        const JBro::ScriptContextBlock block = MakeProbeBlock(extension);

        JBro::ScriptDLLLoader loader;
        Check(loader.Load("probe.dll", platform, &block, 1),
            "valid script module must load");
        Check(loader.IsLoaded() && loader.GetGeneration() == 1,
            "first activation must publish generation one");
        Check(probe.receivedSystems.AbiVersion == JBro::SystemContextAbiVersion
            && probe.receivedServices.AbiVersion == JBro::ServiceContextAbiVersion
            && probe.receivedExtensionBlock
            && probe.receivedExtension.value == extension.value,
            "module must copy the host's common and extension context values");
        Check(loader.GetSymbol("ProbeSymbol") ==
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x5678)),
            "loaded module symbols must remain queryable");

        Check(loader.Reload(platform, &block, 1),
            "valid script module must reload from its stored path");
        Check(loader.IsLoaded() && loader.GetGeneration() == 2,
            "successful reload must advance generation exactly once");
        Check(probe.loadCalls == 2 && probe.unloadCalls == 1,
            "reload must deactivate the old module before activating the new one");
        Check(events.count >= 6
            && events.values[2] == Event::ModuleUnload
            && events.values[3] == Event::PlatformUnload
            && events.values[4] == Event::PlatformLoad
            && events.values[5] == Event::ModuleLoad,
            "reload order must be module unload, DLL release, DLL load, module load");

        loader.Unload(platform);
        Check(false == loader.IsLoaded() && loader.GetGeneration() == 3,
            "explicit unload must invalidate the active generation");
        Check(probe.unloadCalls == 2 && platform.unloadCalls == 2,
            "explicit unload must run the module hook and release the DLL once");
        Check(loader.GetSymbol("ProbeSymbol") == nullptr,
            "unloaded modules must not expose stale symbols");
    }

    void TestFailedReloadInvalidatesOldModule()
    {
        EventLog events;
        ModuleProbe probe{&events};
        ResetApi(probe);
        LoaderPlatform platform(events);
        BindCommonContexts();
        ProbeExtension extension;
        const JBro::ScriptContextBlock block = MakeProbeBlock(extension);

        JBro::ScriptDLLLoader loader;
        Check(loader.Load("probe.dll", platform, &block, 1),
            "reload failure test requires an active module");
        platform.exposeEntryPoint = false;
        Check(false == loader.Reload(platform, &block, 1),
            "reload must fail when the replacement entry point is missing");
        Check(false == loader.IsLoaded() && loader.GetGeneration() == 2,
            "failed reload must still invalidate references to the old module");
        Check(probe.unloadCalls == 1 && platform.unloadCalls == 2,
            "failed reload must release both the old module and rejected candidate");
    }

    struct ProbeFiles
    {
        wchar_t directory[MAX_PATH]{};
        wchar_t sourcePath[MAX_PATH]{};
        wchar_t replacementPath[MAX_PATH]{};
        wchar_t dllPath[MAX_PATH]{};

        ~ProbeFiles()
        {
            if (dllPath[0] != L'\0')
            {
                DeleteFileW(dllPath);
            }
            if (directory[0] != L'\0')
            {
                RemoveDirectoryW(directory);
            }
        }
    };

    std::uint32_t CountShadowLibraries(const ProbeFiles& files)
    {
        wchar_t pattern[MAX_PATH + 64]{};
        Check(swprintf_s(
            pattern,
            L"%ls.jbro.*.dll",
            files.dllPath) > 0,
            "shadow DLL search pattern must fit");

        WIN32_FIND_DATAW entry{};
        const HANDLE search = FindFirstFileW(pattern, &entry);
        if (search == INVALID_HANDLE_VALUE)
        {
            Check(GetLastError() == ERROR_FILE_NOT_FOUND,
                "shadow DLL search must fail only when no copy exists");
            return 0;
        }

        std::uint32_t count = 1;
        while (FindNextFileW(search, &entry) != FALSE)
        {
            ++count;
        }
        const DWORD enumerationError = GetLastError();
        FindClose(search);
        Check(enumerationError == ERROR_NO_MORE_FILES,
            "shadow DLL enumeration must finish normally");
        return count;
    }

    void PrepareRealProbe(ProbeFiles& files, JBro::String& utf8Path)
    {
        wchar_t executablePath[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameW(
            nullptr, executablePath, MAX_PATH);
        Check(executableLength != 0 && executableLength < MAX_PATH,
            "test executable path must fit the Windows path buffer");
        wchar_t* fileName = std::wcsrchr(executablePath, L'\\');
        Check(fileName != nullptr,
            "test executable path must contain its directory");
        *(fileName + 1) = L'\0';

        Check(wcscpy_s(files.sourcePath, executablePath) == 0
            && wcscat_s(files.sourcePath, L"JBroScriptModuleProbe.dll") == 0,
            "probe source path must fit the Windows path buffer");
        Check(wcscpy_s(files.replacementPath, executablePath) == 0
            && wcscat_s(files.replacementPath, L"JBroScriptModuleProbeV2.dll") == 0,
            "replacement probe path must fit the Windows path buffer");

        wchar_t temporaryRoot[MAX_PATH]{};
        const DWORD rootLength = GetTempPathW(MAX_PATH, temporaryRoot);
        Check(rootLength != 0 && rootLength < MAX_PATH,
            "Windows temporary path must be available");
        Check(swprintf_s(
            files.directory,
            L"%lsJBro_H4_한글_%lu",
            temporaryRoot,
            GetCurrentProcessId()) > 0,
            "probe directory path must fit the Windows path buffer");
        if (CreateDirectoryW(files.directory, nullptr) == FALSE)
        {
            Check(GetLastError() == ERROR_ALREADY_EXISTS,
                "probe directory must be creatable");
        }
        Check(wcscpy_s(files.dllPath, files.directory) == 0
            && wcscat_s(files.dllPath, L"\\Player.dll") == 0,
            "probe DLL path must fit the Windows path buffer");
        DeleteFileW(files.dllPath);
        Check(CopyFileW(files.sourcePath, files.dllPath, TRUE) != FALSE,
            "built probe DLL must copy into a Korean path");

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, files.dllPath, -1,
            nullptr, 0, nullptr, nullptr);
        Check(utf8Length > 0,
            "probe path must convert to UTF-8");
        utf8Path.resize(static_cast<std::size_t>(utf8Length));
        Check(WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, files.dllPath, -1,
            utf8Path.data(), utf8Length, nullptr, nullptr) == utf8Length,
            "probe path UTF-8 conversion must fill the destination");
        utf8Path.resize(static_cast<std::size_t>(utf8Length - 1));
    }

    void TestRealDllRoundTripFromKoreanPath()
    {
        ProbeFiles files;
        JBro::String utf8Path;
        PrepareRealProbe(files, utf8Path);

        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory),
            "Windows platform must initialize for the real DLL probe");

        JBro::SystemContext systems;
        JBro::ServiceContext services;
        JBro::Framework2DServiceContext frameworkServices;
        JBro::Framework2DSystemContext frameworkSystems;
        frameworkSystems.Physics2D = reinterpret_cast<JBro::System::IPhysics2DSystem*>(
            static_cast<std::uintptr_t>(0x12345678));
        JBro::BindSystemContext(systems);
        JBro::BindServiceContext(services);
        const JBro::ScriptContextBlock blocks[] = {
            JBro::MakeFramework2DServiceContextBlock(frameworkServices),
            JBro::MakeFramework2DSystemContextBlock(frameworkSystems)};

        JBro::ScriptDLLLoader loader;
        Check(loader.Load(utf8Path.c_str(), platform, blocks, 2),
            "real script DLL must load from a Korean UTF-8 path");
        Check(CountShadowLibraries(files) == 1,
            "a loaded script module must own exactly one shadow DLL");
        using ReadBool = bool (*)() noexcept;
        using ReadU32 = std::uint32_t (*)() noexcept;
        using ReadAddress = std::uintptr_t (*)() noexcept;
        const auto isLoaded = reinterpret_cast<ReadBool>(
            loader.GetSymbol("JBroScriptProbe_IsLoaded"));
        const auto getSystemAbi = reinterpret_cast<ReadU32>(
            loader.GetSymbol("JBroScriptProbe_GetSystemAbi"));
        const auto getServiceAbi = reinterpret_cast<ReadU32>(
            loader.GetSymbol("JBroScriptProbe_GetServiceAbi"));
        const auto getFrameworkAbi = reinterpret_cast<ReadU32>(
            loader.GetSymbol("JBroScriptProbe_GetFramework2DAbi"));
        const auto getPhysicsSystem = reinterpret_cast<ReadAddress>(
            loader.GetSymbol("JBroScriptProbe_GetPhysicsSystem"));
        const auto getRevision = reinterpret_cast<ReadU32>(
            loader.GetSymbol("JBroScriptProbe_GetRevision"));
        Check(isLoaded != nullptr && getSystemAbi != nullptr
            && getServiceAbi != nullptr && getFrameworkAbi != nullptr
            && getPhysicsSystem != nullptr && getRevision != nullptr,
            "real script probe exports must remain queryable while loaded");
        Check(isLoaded()
            && getSystemAbi() == JBro::SystemContextAbiVersion
            && getServiceAbi() == JBro::ServiceContextAbiVersion
            && getFrameworkAbi() == JBro::Framework2DServiceContextAbiVersion,
            "real script DLL must bind its module-local context copies");
        Check(getPhysicsSystem() == reinterpret_cast<std::uintptr_t>(frameworkSystems.Physics2D),
            "real script DLL must receive the host's system pointer value");
        Check(getRevision() == 1,
            "the initial script DLL must expose revision one");

        Check(DeleteFileW(files.dllPath) != FALSE,
            "loaded script DLL must not lock the compiler output path");
        Check(CopyFileW(files.replacementPath, files.dllPath, TRUE) != FALSE,
            "a rebuilt script DLL must replace the source path while the old module runs");

        Check(loader.Reload(platform, blocks, 2),
            "real script DLL must unload and reload from a Korean path");
        Check(loader.GetGeneration() == 2,
            "real DLL reload must advance its invalidation generation");
        const auto getReloadedRevision = reinterpret_cast<ReadU32>(
            loader.GetSymbol("JBroScriptProbe_GetRevision"));
        Check(getReloadedRevision != nullptr && getReloadedRevision() == 2,
            "reload must execute code from the replacement script DLL");
        Check(CountShadowLibraries(files) == 1,
            "reload must delete the old shadow DLL before retaining its replacement");
        loader.Unload(platform);
        Check(CountShadowLibraries(files) == 0,
            "unload must delete the final shadow DLL");
        Check(GetModuleHandleW(files.dllPath) == nullptr,
            "real script DLL must no longer be resident after unload");

        JBro::BindSystemContext({});
        JBro::BindServiceContext({});
        platform.Shutdown();
    }
}

int RunScriptDLLLoaderTests()
{
    TestRejectsInvalidModuleAbiBeforeCallingModule();
    TestRequiresExactUniqueContextBlocks();
    TestRejectsCommonContextAbiBeforeOpeningDll();
    TestFailedModuleActivationRollsBack();
    TestLoadReloadAndUnloadOrder();
    TestFailedReloadInvalidatesOldModule();
    TestRealDllRoundTripFromKoreanPath();
    std::cout << "Script DLL loader tests passed.\n";
    return 0;
}
