#include <JBro/Host/DebugDrawSystem.h>
#include <JBro/Host/RandomSystem.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Host/ScriptDLLLoader.h>
#include <JBro/InputTypes/ServiceContext.h>
#include <JBro/SaveTypes/ServiceContext.h>
#include <JBro/Host/GameLocalization.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>

#include <Windows.h>

#include <cstring>
#include <filesystem>
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

        JBro::JArrayView<JBro::InputEvent> GetInputEvents() const override
        {
            return {};
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

    // **로드 문맥의 필수 포인터**는 하나라도 비면 거절한다. 모듈은 받은 포인터를 모두 제 접근점에 묶으므로, 빈 것을 받아들이면
    // DLL 이 제 사본(빈 저장소·빈 이름표)을 보고도 성공한 것처럼 돈다. 글자 저장소(ABI 5)도 같다.
    void TestTheLoadContextNeedsEveryHostTable()
    {
        const auto valid = []() {
            JBro::ScriptModuleLoadContext context;
            context.Systems = &JBro::GetSystemContext();
            context.Services = &JBro::GetServiceContext();
            context.Registry = &JBro::Internal::InstanceRegistry::Local();
            context.Names = &JBro::NameTable::Local();
            context.Scripts = &JBro::ScriptRegistry::Local();
            context.Texts = &JBro::TextStore::Local();
            return context;
        };
        Check(JBro::ValidateScriptModuleLoadContext(valid()), "a context with every host table is valid");
        JBro::ScriptModuleLoadContext missing = valid();
        missing.Texts = nullptr;
        Check(false == JBro::ValidateScriptModuleLoadContext(missing), "a context without the text store is refused");
        missing = valid();
        missing.Names = nullptr;
        Check(false == JBro::ValidateScriptModuleLoadContext(missing), "a context without the name table is refused");
        missing = valid();
        missing.Scripts = nullptr;
        Check(false == JBro::ValidateScriptModuleLoadContext(missing), "a context without the script registry is refused");
        missing = valid();
        missing.Registry = nullptr;
        Check(false == JBro::ValidateScriptModuleLoadContext(missing), "a context without the instance registry is refused");
        missing = valid();
        missing.StructSize = 64;
        Check(false == JBro::ValidateScriptModuleLoadContext(missing), "the ABI 4 size is refused");
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
        const auto getRegistry = reinterpret_cast<ReadAddress>(
            loader.GetSymbol("JBroScriptProbe_GetRegistry"));
        const auto getLocalRegistry = reinterpret_cast<ReadAddress>(
            loader.GetSymbol("JBroScriptProbe_GetLocalRegistry"));
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
        Check(getRegistry != nullptr && getLocalRegistry != nullptr,
            "the real script probe must expose both registry addresses");
        Check(getRegistry() == reinterpret_cast<std::uintptr_t>(
                &JBro::Internal::InstanceRegistry::Local()),
            "a loaded script DLL must resolve references through the host registry");

        // 이름표도 같은 방식으로 붙어야 한다. 붙지 않으면 DLL 이 호스트가 지은
        // 태그의 원문을 되찾지 못하고 빈 문자열만 본다.
        using ResolveName = const char* (*)(std::uint64_t) noexcept;
        const auto getNameTable = reinterpret_cast<ReadAddress>(
            loader.GetSymbol("JBroScriptProbe_GetNameTable"));
        const auto resolveName = reinterpret_cast<ResolveName>(
            loader.GetSymbol("JBroScriptProbe_ResolveName"));
        Check(getNameTable != nullptr && resolveName != nullptr,
            "the real script probe must expose its name table view");
        Check(getNameTable() == reinterpret_cast<std::uintptr_t>(&JBro::NameTable::Local()),
            "a loaded script DLL must resolve names through the host name table");
        const JBro::NameId hostName = JBro::NameTable::Local().Intern("host side name");
        const char* fromDll = resolveName(hostName);
        Check(fromDll != nullptr && std::strcmp(fromDll, "host side name") == 0,
            "a script DLL must read back a name the host interned");

        // 글자 저장소도 붙는다(D-211). DLL 쪽 코덱이 호스트가 쓴 글자를 읽어야 스크립트 타입의 `TextId` 필드가 파일에 남는다.
        using WriteText = std::uint32_t (*)(std::uint32_t, std::uint32_t, char*, std::uint32_t) noexcept;
        const auto getTextStore = reinterpret_cast<ReadAddress>(loader.GetSymbol("JBroScriptProbe_GetTextStore"));
        const auto writeText = reinterpret_cast<WriteText>(loader.GetSymbol("JBroScriptProbe_WriteText"));
        Check(getTextStore != nullptr && writeText != nullptr, "the real script probe must expose its text store view");
        Check(getTextStore() == reinterpret_cast<std::uintptr_t>(&JBro::TextStore::Local()),
            "a loaded script DLL must keep text in the host text store");
        const JBro::TextId hostText = JBro::TextStore::Local().Create("from the host", 13);
        char written[64] = {};
        Check(writeText(hostText.index, hostText.generation, written, sizeof(written)) != 0
                && std::strcmp(written, "from the host") == 0,
            "the script DLL's codec reads the text the host wrote");
        JBro::TextStore::Local().Destroy(hostText);

        // H5 의 알맹이다. 타입은 DLL 안에만 있고 호스트는 정의를 보지 못하는데,
        // 이름 하나로 만들어 붙일 수 있어야 한다.
        const auto getScriptRegistry = reinterpret_cast<ReadAddress>(
            loader.GetSymbol("JBroScriptProbe_GetScriptRegistry"));
        const auto getRegisteredSize = reinterpret_cast<ReadU32>(
            loader.GetSymbol("JBroScriptProbe_GetRegisteredScriptSize"));
        Check(getScriptRegistry != nullptr && getRegisteredSize != nullptr,
            "the probe must expose its script registry view");
        Check(getScriptRegistry() == reinterpret_cast<std::uintptr_t>(&JBro::ScriptRegistry::Local()),
            "a loaded script DLL must register into the host script registry");

        const JBro::ScriptTypeInfo* registered =
            JBro::ScriptRegistry::Local().Find("Probe::RegisteredScript");
        Check(registered != nullptr, "the host must see the type the DLL registered");
        Check(registered->size == getRegisteredSize(),
            "the host must allocate the size the DLL reports, not a guess");
        Check(registered->Construct != nullptr && registered->Destruct != nullptr,
            "a registered type must carry both halves of its lifetime");

        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("scripted by name");
            JBro::GameScriptBase* script = canvas.AttachScript(object, "Probe::RegisteredScript");
            Check(script != nullptr, "the canvas must attach a script it only knows by name");
            Check(script->GetTypeId() == registered->typeId,
                "the attached instance must be the type that was registered");
            Check(script->GetInstanceId() != JBro::InvalidInstanceId,
                "a script attached by name must be registered like any other component");
            Check(canvas.AttachScript(object, "Probe::NoSuchScript") == nullptr,
                "an unregistered name must attach nothing");

            JBro::Array<JBro::GameScriptBase*> collected;
            canvas.CollectScripts(collected);
            Check(collected.Size() == 1 && collected[0] == script,
                "a script attached by name must show up in the schedule like any other");

            // 슬롯은 재사용된다. 죽은 스크립트를 보던 참조가 그 자리에 들어온
            // 새 스크립트를 가리키게 되면 안 된다 — 남의 수명을 자기 것처럼 보게 된다.
            JBro::SafePtr<JBro::ComponentBase> stale = script->SafeFromThis();
            Check(stale.IsValid(), "a live script must hand out a valid reference");

            Check(canvas.DestroyObject(object), "the scripted object must be destroyed");
            canvas.FlushPendingDestroy();
            canvas.CollectScripts(collected);
            Check(collected.IsEmpty(), "destroying the owner must take its named script with it");
            Check(false == stale.IsValid(),
                "a reference to a destroyed script must not stay valid");

            JBro::GameObject* second = canvas.CreateObject("second scripted");
            JBro::GameScriptBase* reborn = canvas.AttachScript(second, "Probe::RegisteredScript");
            Check(reborn != nullptr, "the pool must serve a second script");
            Check(false == stale.IsValid(),
                "a stale reference must not revive on the slot the new script took");
            Check(stale.TryGet() != reborn,
                "a stale reference must never resolve to whatever reused its slot");
        }
        Check(getLocalRegistry() != getRegistry(),
            "the DLL's own statically linked registry must be a different object");
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

    // 호스트가 프로젝트를 열면서 스크립트 DLL 까지 싣는 경로다(§10 (A)).
    // 경로가 어느 파일에서 오는지는 여기서 정하지 않는다 — 호스트는 문자열만 받는다.
    class ScriptedFramework final : public JBro::IFramework
    {
    public:
        bool Initialize(const JBro::FrameworkContext&) override
        {
            return true;
        }

        bool BindScriptContexts() noexcept override
        {
            // 공통 컨텍스트는 호스트의 것이다(D-241) - 시계·난수를 호스트가 묶었다. 프레임워크는 제 블록만 낸다.
            m_frameworkSystems.Physics2D = reinterpret_cast<JBro::System::IPhysics2DSystem*>(
                static_cast<std::uintptr_t>(0x0BADF00D));
            m_blocks[0] = JBro::MakeFramework2DServiceContextBlock(m_frameworkServices);
            m_blocks[1] = JBro::MakeFramework2DSystemContextBlock(m_frameworkSystems);
            m_blockCount = 2;
            bound = true;
            return true;
        }

        void UnbindScriptContexts() noexcept override
        {
            // 호스트가 DLL 을 먼저 내렸어야 한다. 아직 실려 있다면 순서가 뒤집힌 것이다.
            // 미리 채워 둔 값이 아니라 지금 물어봐야 의미가 있다.
            unbindSawLoadedModule = host != nullptr && host->GetScriptModule().IsLoaded();
            unbindCount++;
            m_blockCount = 0;
            bound = false;
        }

        JBro::JArrayView<JBro::ScriptContextBlock> GetScriptContextBlocks() const noexcept override
        {
            return {m_blocks, m_blockCount};
        }

        void Update() override
        {
        }

        JBro::RenderResult Render() override
        {
            return JBro::RenderResult::NothingToSubmit;
        }

        void Shutdown() override
        {
            shutdowns++;
        }

        const JBro::EngineInstance* host = nullptr;
        bool bound = false;
        int shutdowns = 0;
        int unbindCount = 0;
        bool unbindSawLoadedModule = false;

    private:
        JBro::Framework2DServiceContext   m_frameworkServices;
        JBro::Framework2DSystemContext    m_frameworkSystems;
        JBro::ScriptContextBlock          m_blocks[2];
        std::uint32_t                     m_blockCount = 0;
    };

    void TestHostOpensAProjectWithItsScriptModule()
    {
        ProbeFiles files;
        JBro::String utf8Path;
        PrepareRealProbe(files, utf8Path);

        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the host script test");
        if (false == rhi.Initialize(memory))
        {
            // 이 기계에 D3D12 장치가 없으면 호스트를 세울 수 없다. 조용히 건너뛰지 않고 남긴다.
            std::cout << "  [skip] no D3D12 device; host script module wiring not exercised" << std::endl;
            platform.Shutdown();
            return;
        }

        JBro::EngineConfig config;
        config.window.visible = false;
        config.window.width = 64;
        config.window.height = 64;
        // 세이브는 임시 폴더에 쓴다 - 시험이 사용자 폴더에 세이브를 남기지 않는다(D-218).
        const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "JBroHostSaveProbe";
        std::error_code saveError;
        std::filesystem::remove_all(saveRoot, saveError);
        {
            const std::u8string text = saveRoot.generic_u8string();
            config.saveFolder = JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
        }
        JBro::EngineInstance engine;
        Check(engine.Initialize(config, platform, rhi), "host must initialize for the script test");
        Check(false == engine.GetScriptModule().IsLoaded(),
            "a host with no project must hold no script module");

        ScriptedFramework framework;
        framework.host = &engine;
        Check(engine.OpenProject(framework, utf8Path.c_str()),
            "the host must open a project together with its script module");
        Check(framework.bound, "the host must bind contexts before loading the module");
        Check(engine.GetScriptModule().IsLoaded(),
            "opening a project with a module path must leave that module loaded");
        Check(engine.GetScriptModule().GetSymbol("JBroScriptProbe_IsLoaded") != nullptr,
            "the loaded module must be queryable through the host");

        // 게임 입력이 DLL 까지 닿는다(D-214). 창에 넣은 키를 엔진이 틱에서 접고, DLL 은 자기 사본의 서비스로
        // 그것을 읽는다 - 호스트가 입력 블록을 내지 않았거나 DLL 이 묶지 않았으면 여기서 거짓이다.
        using IsKeyDown = bool (*)(std::uint16_t) noexcept;
        const auto isKeyDown = reinterpret_cast<IsKeyDown>(
            engine.GetScriptModule().GetSymbol("JBroScriptProbe_IsKeyDown"));
        Check(isKeyDown != nullptr, "the probe must export its key query");
        const auto space = static_cast<std::uint16_t>(JBro::Key::Space);
        Check(false == isKeyDown(space), "nothing is held before any input arrives");
        const HWND window = reinterpret_cast<HWND>(engine.GetMainWindow().value);
        PostMessageW(window, WM_KEYDOWN, VK_SPACE, 0);
        Check(engine.Tick(0.016f), "the host must tick with the script module loaded");
        Check(isKeyDown(space), "a key posted to the game window must reach the service inside the script DLL");
        // 호스트 안에서 정적으로 붙인 스크립트는 호스트 모듈의 사본을 읽는다. 엔진이 그 사본에도 묶었어야 한다.
        Check(JBro::GetInputServices().Input.Keyboard().IsDown(JBro::Key::Space),
            "the host's own copy of the service must see the same key");
        PostMessageW(window, WM_KEYUP, VK_SPACE, static_cast<LPARAM>(0xC0000001u));
        Check(engine.Tick(0.016f), "the host must keep ticking");
        Check(false == isKeyDown(space), "and releasing it must reach the DLL too");

        // 프로젝트의 입력 액션이 곧바로 걸린다(D-214). 설정 창이 저장하면 부르는 길(`SetProjectFile`)이다.
        JBro::ProjectFile inputProject = engine.GetProjectFile();
        JBro::ProjectInputAction jumpAction;
        jumpAction.name = "Jump";
        JBro::ProjectInputBinding jumpKey;
        jumpKey.code = static_cast<std::uint16_t>(JBro::Key::Space);
        jumpAction.bindings.Add(jumpKey);
        inputProject.inputActions.Add(jumpAction);
        engine.SetProjectFile(inputProject);
        const JBro::InputActionId jump = JBro::MakeNameId("Jump");
        PostMessageW(window, WM_KEYDOWN, VK_SPACE, 0);
        Check(engine.Tick(0.016f), "the host must tick with the new actions");
        Check(JBro::GetInputServices().Input.GetView().IsActionPressed(jump),
            "a key bound in the project presses its action through the service");
        PostMessageW(window, WM_KEYUP, VK_SPACE, static_cast<LPARAM>(0xC0000001u));
        Check(engine.Tick(0.016f), "the host must keep ticking");

        // 시간과 난수가 DLL 까지 닿는다(D-241). DLL 은 제 사본의 서비스로 호스트의 시계와 난수 흐름을 읽는다 - 호스트가 공통 시스템
        // 컨텍스트를 채우지 않았으면 델타는 0 이고, DLL 이 묶지 않았으면 제 사본의 고정 씨앗 흐름에서 뽑아 엔진 씨앗이 바뀌지 않는다.
        using GetDelta = float (*)() noexcept;
        using GetFrames = std::uint64_t (*)() noexcept;
        using SetSeed = void (*)(std::uint64_t) noexcept;
        using RandomRange = std::int32_t (*)(std::int32_t, std::int32_t) noexcept;
        const auto getDelta = reinterpret_cast<GetDelta>(engine.GetScriptModule().GetSymbol("JBroScriptProbe_GetDeltaTime"));
        const auto getFrames = reinterpret_cast<GetFrames>(engine.GetScriptModule().GetSymbol("JBroScriptProbe_GetFrameCount"));
        const auto setSeed = reinterpret_cast<SetSeed>(engine.GetScriptModule().GetSymbol("JBroScriptProbe_SetRandomSeed"));
        const auto randomRange = reinterpret_cast<RandomRange>(engine.GetScriptModule().GetSymbol("JBroScriptProbe_RandomRange"));
        Check(getDelta != nullptr && getFrames != nullptr && setSeed != nullptr && randomRange != nullptr,
            "the probe must export its time and random queries");
        Check(engine.Tick(0.02f), "the host must tick for the time probe");
        Check(getDelta() == 0.02f, "the script DLL must read the host clock's delta through its own service copy");
        Check(getFrames() == engine.GetTime()->GetFrameTime().frameCount && getFrames() > 0,
            "and the host's frame count");
        setSeed(99u);
        Check(engine.GetRandom()->GetSeed() == 99u, "a seed set inside the DLL must reach the engine stream");
        JBro::RandomStream reference(99u);
        Check(randomRange(0, 1000) == reference.Range(0, 1000), "and the DLL must draw from that stream");
        Check(JBro::GetServiceContext().Random.Range(0, 1000) == reference.Range(0, 1000),
            "the host's copy of the service shares the same engine stream");
        // DLL 이 그린 디버그 선이 엔진의 저장소에 들어오고, 엔진이 다음 프레임 첫머리에 거둔다(D-242).
        using DrawLine = void (*)() noexcept;
        const auto drawLine = reinterpret_cast<DrawLine>(engine.GetScriptModule().GetSymbol("JBroScriptProbe_DrawLine"));
        Check(drawLine != nullptr && engine.GetDebugDraw() != nullptr, "the probe must export its line and the engine own a store");
        drawLine();
        Check(engine.GetDebugDraw()->GetLineCount() == 1, "a line drawn inside the script DLL must land in the host's store");
        Check(engine.Tick(0.02f) && engine.GetDebugDraw()->GetLineCount() == 0, "and the engine must clear a one-frame line the next frame");
        drawLine();
        engine.RestartGameTime();
        Check(engine.GetDebugDraw()->GetLineCount() == 0, "restarting the game time clears the lines of the last play");
        // 멈춤과 한 프레임 진행은 엔진의 것이다(D-241). 멈추면 DLL 이 읽는 델타가 0 이고, 한 프레임 진행은 고정 델타 한 번이다.
        engine.SetSimulationEnabled(false);
        Check(engine.Tick(0.02f) && getDelta() == 0.0f, "a paused engine must hand the script a zero delta");
        engine.StepSimulation();
        Check(engine.Tick(0.5f) && getDelta() == engine.GetTime()->GetSettings().fixedDeltaTime,
            "a single-frame step must move exactly one fixed delta, whatever the frame took");
        Check(engine.Tick(0.02f) && getDelta() == 0.0f, "and the frame after it is paused again");
        engine.GetTime()->SetTimeScale(4.0f);
        engine.SetSimulationEnabled(true);
        engine.RestartGameTime();
        Check(engine.GetTime()->GetFrameTime().timeScale == 1.0f && engine.GetTime()->GetFrameTime().time == 0.0,
            "restarting the game time must undo the scale a game set and zero the game clock");

        // DLL 안의 스크립트가 세이브를 쓰고 되읽는다(D-218). 읽은 바이트는 DLL 의 힙에 놓인다.
        using SaveRoundTrip = bool (*)(const char*, const char*) noexcept;
        const auto saveRoundTrip = reinterpret_cast<SaveRoundTrip>(
            engine.GetScriptModule().GetSymbol("JBroScriptProbe_SaveRoundTrip"));
        Check(saveRoundTrip != nullptr, "the probe must export its save round trip");
        Check(saveRoundTrip("probe.yaml", "score: 42\n"), "a save written inside the script DLL reads back there");
        Check(std::filesystem::is_regular_file(saveRoot / "probe.yaml", saveError),
            "and lands in the folder the host was given");
        JBro::String hostRead;
        Check(JBro::GetSaveServices().Save.ReadText("probe.yaml", hostRead) && hostRead == "score: 42\n",
            "the host's own copy of the service reads the same slot");

        // 리바인딩의 글자도 DLL 이 제 힙에 받는다.
        Check(JBro::GetInputServices().Input.SetActionBinding(jump, 0, [] {
            JBro::InputBinding binding;
            binding.code = static_cast<std::uint16_t>(JBro::Key::Enter);
            return binding;
        }()), "the host's copy rebinds the action");
        using WriteOverrides = std::size_t (*)(char*, std::size_t) noexcept;
        const auto writeOverrides = reinterpret_cast<WriteOverrides>(
            engine.GetScriptModule().GetSymbol("JBroScriptProbe_WriteBindingOverrides"));
        Check(writeOverrides != nullptr, "the probe must export its binding text");
        char overrides[128] = {};
        const std::size_t overridesSize = writeOverrides(overrides, sizeof(overrides));
        Check(JBro::String(overrides, overridesSize) == "Jump: \"Key Enter\"\n",
            "the script DLL receives the changed binding by name");

        // DLL 안의 스크립트가 로케일을 바꾸면 호스트의 것이 바뀐다(D-226). 표가 없는 프로젝트라 키는 그대로 돌아온다.
        using Localize = std::size_t (*)(const char*, const char*, char*, std::size_t) noexcept;
        const auto localize = reinterpret_cast<Localize>(engine.GetScriptModule().GetSymbol("JBroScriptProbe_Localize"));
        Check(localize != nullptr, "the probe must export its localization call");
        char localized[32] = {};
        const std::size_t localizedSize = localize("en-US", "menu.start", localized, sizeof(localized));
        Check(JBro::String(localized, localizedSize) == "menu.start", "a key with no table comes back as itself inside the DLL");
        Check(engine.GetLocalization() != nullptr && engine.GetLocalization()->GetLocaleName() == "en-US",
            "and the locale the DLL set is the host's");

        engine.CloseProject();
        Check(framework.unbindCount == 1, "closing must unbind the contexts once");
        Check(false == engine.GetScriptModule().IsLoaded(),
            "closing a project must unload its script module");
        Check(false == framework.unbindSawLoadedModule,
            "the module must go down before the contexts it borrowed");
        Check(framework.shutdowns == 1, "closing must shut the framework down once");

        // 실패한 모듈은 프로젝트를 막지 않는다(D-98). 아직 한 번도 빌드하지 않은 프로젝트가
        // 그 모양이고, 여기서 막으면 그것을 빌드할 에디터가 열리지 않는다.
        // 대신 못 실었다는 사실이 남아야 한다.
        ScriptedFramework second;
        Check(engine.OpenProject(second, "no such module.dll"),
            "a project whose script module fails to load must still open");
        Check(false == engine.GetScriptModule().IsLoaded(),
            "a failed module load must leave nothing loaded");
        Check(false == engine.IsScriptModuleLoaded(),
            "and the host must not claim it loaded one");
        Check(engine.GetScriptModuleError().find("no such module.dll") != JBro::String::npos,
            "naming the module it failed on");
        Check(engine.GetFramework() == &second, "the framework stays with the open project");
        engine.CloseProject();
        Check(engine.GetScriptModuleError().empty(),
            "closing the project must clear that complaint");

        // 경로가 없으면 스크립트 없이 여는 것과 같다.
        ScriptedFramework third;
        Check(engine.OpenProject(third), "a project without scripts must still open");
        Check(false == engine.GetScriptModule().IsLoaded(),
            "a project without a module path must load nothing");
        engine.CloseProject();

        engine.Shutdown();
        rhi.Shutdown();
        platform.Shutdown();
    }
}

int RunScriptDLLLoaderTests()
{
    TestRejectsInvalidModuleAbiBeforeCallingModule();
    TestTheLoadContextNeedsEveryHostTable();
    TestRequiresExactUniqueContextBlocks();
    TestRejectsCommonContextAbiBeforeOpeningDll();
    TestFailedModuleActivationRollsBack();
    TestLoadReloadAndUnloadOrder();
    TestFailedReloadInvalidatesOldModule();
    TestRealDllRoundTripFromKoreanPath();
    TestHostOpensAProjectWithItsScriptModule();
    std::cout << "Script DLL loader tests passed.\n";
    return 0;
}
