#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Code/SourceDebugger.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/ScriptBuild.h>
#include <JBro/Editor/ScriptProject.h>
#include <JBro/Host/ScriptDLLLoader.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/NameTable.h>
#include "../Modules/JBroEditor/Source/Panel/CodeEditorPanel.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace JBro;
    namespace fs = std::filesystem;
    constexpr Float Frame = 1.0f / 60.0f;
    constexpr UInt32 BreakLine = 6;
    constexpr UInt32 RequestedBreakLine = 5;
    constexpr const char* ScriptType = "NativeSourceProbe";
    constexpr const char* ScriptText = "script NativeSourceProbe\n{\n"
        "    [prop] public Float callbacks = 0.0\n"
        "    public fn OnUpdate() callback\n    {\n"
        "        callbacks = callbacks + 1.0\n    }\n}\n";

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    String Utf8(const fs::path& path)
    {
        const auto bytes = path.generic_u8string();
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    struct Files
    {
        fs::path root = fs::temp_directory_path() / ("JBroSourceDebugger-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Files()
        {
            fs::create_directories(root / "Contents/Scripts");
            fs::create_directories(root / "Contents/Assets");
            Write("Game.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\n"
                "ResolutionWidth: 640\nResolutionHeight: 480\nAssetDirectory: Contents/Assets\n"
                "ScriptSourceDirectory: Contents\nScriptOutputLibraryPath: x64/Debug/GameScript.dll\n"
                "Build:\n  ProductName: NativeSourceProbe\n");
            Write("Contents/Scripts/NativeSourceProbe.jscript", ScriptText);
        }
        void Write(const char* name, std::string_view text)
        {
            std::ofstream output(root / name, std::ios::binary);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            Check(output.good(), "the native debugger fixture source is written");
        }
    };

    template<typename Predicate>
    void Pump(EditorApplication& editor, Predicate done, std::chrono::seconds timeout, const char* message)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (false == done())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                const String& adapterError = editor.GetSourceDebugger().Client().GetError();
                std::cout << "native debugger timeout: " << message << "; adapter: " << adapterError << std::endl;
                Check(false, message);
            }
            Check(editor.Tick(Frame), "the editor keeps drawing while the native debugger progresses");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    Float CallbackCount(EditorApplication& editor)
    {
        Array<GameScriptBase*> scripts;
        editor.GetCanvas()->CollectScripts(scripts);
        const PropertyTable* table = PropertyRegistry::Lookup(ScriptType);
        if (scripts.Size() != 1 || table == nullptr)
        {
            return -1.0f;
        }
        for (UInt32 index = 0; index < table->count; ++index)
        {
            const PropertyInfo& property = table->properties[index];
            if (std::strcmp(NameTable::Get().Resolve(property.name), "callbacks") == 0 && property.ConstAddress != nullptr)
            {
                return *static_cast<const Float*>(property.ConstAddress(scripts[0]));
            }
        }
        return -1.0f;
    }

    Bool TerminateOwnedAdapter()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        DWORD processId = 0;
        UInt32 matches = 0;
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        {
            if (entry.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(entry.szExeFile, L"JBroDebugAdapter.exe") == 0)
            {
                processId = entry.th32ProcessID;
                ++matches;
            }
        }
        CloseHandle(snapshot);
        if (matches != 1)
        {
            return false;
        }
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
        if (process == nullptr)
        {
            return false;
        }
        const Bool terminated = TerminateProcess(process, 1) != FALSE
            && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
        CloseHandle(process);
        return terminated;
    }

    Code::FieldEditResult PrepareSummaryEdit(EditorApplication& editor, CodeEditorPanel& document)
    {
        Code::LanguageClient& language = *document.GetLanguageClient();
        Pump(editor, [&]
        {
            const auto* summary = language.GetSummary(document.GetPath());
            return summary != nullptr && summary->valid && summary->version == language.GetVersion(document.GetPath());
        }, std::chrono::seconds(20), "the real language server supplies a current script summary");
        const auto* summary = language.GetSummary(document.GetPath());
        Check(summary->types.Size() == 1 && summary->types[0].members.Size() == 2,
            "the native debugger source has one field and one callback");
        const auto& field = summary->types[0].members[0];
        Code::FieldSettings settings;
        settings.replaceInitializer = true;
        settings.initializerPresent = true;
        settings.initializer = "99.0";
        settings.attributes = field.attributes;
        const UInt64 request = language.EditFieldSettings(document.GetPath(), summary->version, field.selectionRange.start, settings);
        Check(request != 0, "the summary produces a real source edit request before the debug break");
        Pump(editor, [&]
        {
            const auto* result = language.GetFieldEditResult(document.GetPath());
            return result != nullptr && result->requestId == request && result->ready;
        }, std::chrono::seconds(20), "the real language server validates the pending field edit");
        const auto* result = language.GetFieldEditResult(document.GetPath());
        Check(result->success && false == result->edits.IsEmpty(), "the held summary edit is valid source before it is locked");
        return *result;
    }

    struct BreakProbe
    {
        CodeEditorPanel* document = nullptr;
        String path;
        Code::FieldEditResult summaryEdit;
        UInt32 hits = 0;
        Bool location = true;
        Bool firstCallback = true;
        Bool installed = true;
        Bool sourceLocked = true;
        Bool summaryLocked = true;
        Bool summaryVersionCurrent = true;
        Bool configurationReady = true;
        Bool breakFramesDrawn = false;
        Bool terminateAdapter = false;
        Bool adapterTerminated = false;
    };

    void OnBreak(void* user, EditorApplication& editor)
    {
        auto& probe = *static_cast<BreakProbe*>(user);
        if (editor.GetScriptBreakFrameCount() == 0)
        {
            ++probe.hits;
            const auto& location = editor.GetScriptBreakLocation();
            probe.location = probe.location && location.line == BreakLine
                && Code::LanguageClient::NormalizePath(location.file) == probe.path;
            probe.firstCallback = probe.firstCallback && CallbackCount(editor) == 0.0f;
            const auto state = editor.GetSourceDebugger().Client().GetState();
            probe.configurationReady = probe.configurationReady
                && (state == Code::DebugState::Ready || state == Code::DebugState::Stopped);
            const auto* breakpoints = editor.GetSourceDebugger().Client().GetBreakpoints(probe.path);
            probe.installed = probe.installed && breakpoints != nullptr && breakpoints->Size() == 1
                && (*breakpoints)[0].verified && (*breakpoints)[0].requestedLine == RequestedBreakLine
                && (*breakpoints)[0].actualLine == BreakLine;
            const String before = probe.document->GetBufferText();
            probe.sourceLocked = probe.sourceLocked && probe.document->IsReadOnly() && editor.GetCommands().IsLocked();
            probe.summaryVersionCurrent = probe.summaryVersionCurrent
                && probe.document->GetLanguageClient()->GetVersion(probe.path) == probe.summaryEdit.version;
            probe.summaryLocked = probe.summaryLocked
                && false == probe.document->ApplySourceEdits(probe.summaryEdit.version, probe.summaryEdit.edits)
                && probe.document->GetBufferText() == before;
            if (probe.terminateAdapter)
            {
                probe.adapterTerminated = TerminateOwnedAdapter();
                if (!probe.adapterTerminated)
                {
                    editor.ContinueScriptBreak();
                }
                return;
            }
        }
        if (probe.terminateAdapter)
        {
            return;
        }
        if (editor.GetScriptBreakFrameCount() >= 2)
        {
            probe.breakFramesDrawn = true;
            const String before = probe.document->GetBufferText();
            probe.sourceLocked = probe.sourceLocked && false == probe.document->GetBuffer().ApplyEdits(probe.summaryEdit.edits)
                && probe.document->GetBufferText() == before;
            // Two stopped UI frames exercise the real native widget read-only state.
            // Every observation path still exits deterministically.
            editor.ContinueScriptBreak();
        }
    }

    void WaitForFirstCallback(EditorApplication& editor, BreakProbe& probe)
    {
        editor.StartDebugSession();
        Check(editor.IsDebugSessionActive() && false == editor.IsSimulationPlaying(),
            "debug start builds and configures before entering simulation");
        Check(false == editor.StartSimulation(), "ordinary play cannot bypass the initial debugger configuration barrier");
        Pump(editor, [&]
        {
            if (editor.IsSimulationPlaying())
            {
                const auto state = editor.GetSourceDebugger().Client().GetState();
                Check(state == Code::DebugState::Ready || state == Code::DebugState::Stopped,
                    "simulation never runs before native debugger configuration is ready");
            }
            if (probe.hits == 0 && false == editor.IsDebugSessionActive())
            {
                std::cout << "native adapter failure: " << editor.GetSourceDebugger().Client().GetError() << std::endl;
                Check(false, "native debugging stays active until the first callback breakpoint");
            }
            return probe.hits > 0;
        }, std::chrono::seconds(120), "the first JBroScript callback reaches the native engine breakpoint");
        Check(probe.hits == 1 && probe.firstCallback && CallbackCount(editor) == 1.0f,
            "the first callback stops before its first statement and continues exactly once");
        Check(probe.location && probe.installed && probe.configurationReady,
            "the actual engine relocates the opening-brace request from line 5 to executable line 6 before the first callback");
        Check(probe.sourceLocked && probe.summaryLocked && probe.summaryVersionCurrent && probe.breakFramesDrawn,
            "the stopped engine locks code and refuses an otherwise valid summary source edit");
        std::cout << "  native breakpoint requested line " << RequestedBreakLine.Get()
            << " verified at executable line " << BreakLine.Get() << " before the first callback\n";
    }

    void StopAndWait(EditorApplication& editor)
    {
        editor.StopDebugSession();
        Pump(editor, [&]
        {
            return false == editor.IsDebugSessionActive() && false == editor.IsScriptDebugServerActive()
                && editor.GetSourceDebugger().Client().GetState() == Code::DebugState::Idle;
        }, std::chrono::seconds(10), "native debugger stop disconnects its child and engine channel");
        Check(false == editor.IsSimulationPlaying(), "stopping native debugging stops simulation");
    }

    struct ExecutableBreakpointSlots
    {
        static constexpr std::size_t PageBytes = 4096;
        EditorApplication& editor;
        std::uint8_t* page = nullptr;
        std::size_t count = 0;
        explicit ExecutableBreakpointSlots(EditorApplication& value) : editor(value) {}
        ~ExecutableBreakpointSlots()
        {
            if (page != nullptr)
            {
                for (std::size_t index = 0; index < count; ++index)
                {
                    editor.ClearScriptBreakpoint(Address(index));
                }
                VirtualFree(page, 0, MEM_RELEASE);
            }
        }
        UInt64 Address(std::size_t index) const
        {
            return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(page + index));
        }
        Bool Fill()
        {
            page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, PageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (page == nullptr)
            {
                return false;
            }
            std::memset(page, 0x90, PageBytes);
            DWORD protection = 0;
            if (false == VirtualProtect(page, PageBytes, PAGE_EXECUTE_READ, &protection))
            {
                return false;
            }
            // These bytes are never executed. Only the real platform breakpoint table is exercised.
            while (count < PageBytes && editor.SetScriptBreakpoint(Address(count)))
            {
                ++count;
            }
            return count > 0 && count < PageBytes;
        }
    };

    void TestActualInstallFailureStopsBeforePlay(EditorApplication& editor, BreakProbe& probe)
    {
        ExecutableBreakpointSlots slots(editor);
        Bool capacityFilled = false;
        Bool simulationRan = false;
        const UInt32 before = probe.hits;
        editor.StartDebugSession();
        Check(editor.GetSourceDebugger().IsActive() && false == editor.IsSimulationPlaying(),
            "installation failure scenario begins behind the real debugger configuration barrier");
        Pump(editor, [&]
        {
            simulationRan = simulationRan || editor.IsSimulationPlaying();
            Check(false == simulationRan, "a failed engine breakpoint installation must never enter simulation");
            const auto state = editor.GetSourceDebugger().Client().GetState();
            if (false == capacityFilled && (state == Code::DebugState::Starting || state == Code::DebugState::Configuring))
            {
                Check(editor.IsScriptDebugServerActive() && slots.Fill(),
                    "the real engine breakpoint slots fill after DLL reload and before its source installation request");
                Check(false == editor.SetScriptBreakpoint(slots.Address(slots.count)),
                    "the production SetScriptBreakpoint implementation rejects an additional executable address");
                capacityFilled = true;
            }
            if (capacityFilled && state == Code::DebugState::Failed && false == editor.GetSourceDebugger().IsActive()
                && false == editor.IsScriptDebugServerActive())
            {
                return true;
            }
            Check(editor.GetSourceDebugger().IsActive(), "the failure scenario reaches the actual engine installation response");
            return false;
        }, std::chrono::seconds(120), "the actual engine installation rejection fails configuration and closes its channel");
        const auto* breakpoints = editor.GetSourceDebugger().Client().GetBreakpoints(probe.path);
        Check(capacityFilled && false == simulationRan && probe.hits == before && CallbackCount(editor) == 0.0f
            && editor.GetSourceDebugger().Client().GetError().find("install") != String::npos
            && breakpoints != nullptr && breakpoints->Size() == 1 && false == (*breakpoints)[0].verified,
            "a real rejected setbp produces configuration failure without a callback or a falsely installed marker");
        Check(editor.SetScriptBreakpoint(slots.Address(0)) && editor.ClearScriptBreakpoint(slots.Address(0)),
            "failure cleanup restores breakpoint capacity before the executable test page is released");
        std::cout << "  actual SetScriptBreakpoint rejection after " << slots.count
            << " installed slots failed configuration without starting simulation\n";
    }

    void TestNativeSourceDebugger()
    {
        Files files;
        std::cout << "  SourceDebugger fixture: " << Utf8(files.root) << std::endl;
        EditorApplication editor;
        BreakProbe probe;
        struct Cleanup
        {
            EditorApplication& editor;
            ~Cleanup()
            {
                editor.SetScriptBreakFrameHook(nullptr, nullptr);
                editor.StopDebugSession();
                editor.Shutdown();
            }
        } cleanup{editor};
        const String preferences = Utf8(files.root / "EditorPreferences.yaml");
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        config.preferencesPath = preferences.c_str();
        Check(editor.Initialize(config), "the native debugger test initializes a real engine device");
        Check(LocalizationTable::Get().Load(editor.GetPlatform(), "Localization", "ko-KR", "en-US"),
            "the real debugger UI loads its Korean error messages");
        Check(false == ScriptBuild::LocateMSBuild(editor.GetPlatform(), Utf8(files.root / "Tools").c_str()).IsEmpty(),
            "the native debugger runtime test requires the real script build toolset");
        String projectError;
        Check(ScriptProject::EnsureProject(editor.GetPlatform(), Utf8(files.root / "Contents").c_str(),
            FrameworkKind::Framework2D, projectError), "the native debugger fixture has a real game DLL project");
        ProjectFileError error;
        Check(editor.OpenProjectFile(Utf8(files.root / "Game.jproject").c_str(), error), "the native debugger project opens");
        Check(editor.EnableEditorUi({64, 48}), "the actual editor panels draw during script breaks");
        Check(editor.BuildScripts(), "the initial JBroScript game DLL build starts");
        Pump(editor, [&]
        {
            return editor.GetScriptBuildState() != EditorApplication::ScriptBuildState::Running;
        }, std::chrono::seconds(120), "the initial JBroScript game DLL build finishes");
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded && editor.IsScriptModuleLoaded(),
            "the initial source produces and loads a real game DLL");
        Object::GameObject* object = editor.GetCanvas()->CreateObject("NativeSourceProbe");
        Check(editor.GetCanvas()->AttachScript(object, ScriptType) != nullptr, "the generated script attaches to the canvas");
        probe.path = Code::LanguageClient::NormalizePath(Utf8(files.root / "Contents/Scripts/NativeSourceProbe.jscript"));
        Check(editor.OpenCodeFile(probe.path), "the native editor opens the script under test");
        Array<EditorPanel*> panels;
        editor.FindPanels(CodeEditorPanel::TypeName, panels);
        Check(panels.Size() == 1, "the runtime fixture has one native source tab");
        probe.document = static_cast<CodeEditorPanel*>(panels[0]);
        probe.summaryEdit = PrepareSummaryEdit(editor, *probe.document);
        editor.GetSourceDebugger().Anchors().Toggle(probe.path, RequestedBreakLine);
        Check(editor.GetSourceDebugger().Anchors().Lines(probe.path).Size() == 1, "the source gutter has a requested breakpoint");
        editor.SetScriptBreakFrameHook(&OnBreak, &probe);
        WaitForFirstCallback(editor, probe);

        editor.SetSimulationPaused(true);
        Pump(editor, [&]
        {
            return editor.GetSourceDebugger().Client().GetState() == Code::DebugState::Ready;
        }, std::chrono::seconds(10), "the adapter observes the continued event before source mismatch checks");
        Code::TextEdit changed;
        changed.range.start = {8, 0};
        changed.range.end = {8, 0};
        changed.newText = "// unsaved source revision\n";
        Check(probe.document->ApplySourceEdits(probe.document->GetLanguageClient()->GetVersion(probe.path), {changed}),
            "source editing resumes after continuing from the breakpoint");
        Pump(editor, [&]
        {
            const auto* breakpoints = editor.GetSourceDebugger().Client().GetBreakpoints(probe.path);
            return false == editor.GetSourceDebugger().Matches(probe.path, probe.document->GetBufferText())
                && breakpoints != nullptr && breakpoints->IsEmpty();
        }, std::chrono::seconds(10), "an unsaved source change makes the requested breakpoint pending and disarms the old code");
        const UInt64 pendingRevision = editor.GetSourceDebugger().Client().GetRevision();
        Pump(editor, [&]
        {
            return editor.GetSourceDebugger().Client().GetRevision() > pendingRevision;
        }, std::chrono::seconds(10), "the adapter acknowledges the empty breakpoint replacement before old code runs");
        Check(editor.GetSourceDebugger().Anchors().Lines(probe.path).Size() == 1, "source mismatch retains the pending gutter anchor");
        const UInt32 beforeMismatchRun = probe.hits;
        editor.SetSimulationPaused(false);
        Check(editor.Tick(Frame), "the older loaded script can run while its source breakpoint is pending");
        Check(probe.hits == beforeMismatchRun && CallbackCount(editor) == 2.0f,
            "the mismatched source breakpoint no longer traps the old DLL");
        StopAndWait(editor);

        const UInt64 generation = editor.GetScriptModule()->GetGeneration();
        probe.hits = 0;
        probe.summaryEdit = PrepareSummaryEdit(editor, *probe.document);
        WaitForFirstCallback(editor, probe);
        Check(editor.GetScriptModule()->GetGeneration() != generation,
            "restarting native debugging saves the source revision and reloads its newly built DLL");
        StopAndWait(editor);
        Check(editor.GetSourceDebugger().Anchors().Lines(probe.path).Size() == 1,
            "disconnect preserves project-session source breakpoints for the next debug run");
        const UInt32 beforeOrdinaryRun = probe.hits;
        Check(editor.StartSimulation() && editor.Tick(Frame), "ordinary simulation starts after native debugger disconnect");
        Check(probe.hits == beforeOrdinaryRun && CallbackCount(editor) == 1.0f,
            "disconnect removed the installed engine breakpoint before ordinary simulation");
        editor.StopSimulation();

        Check(editor.StartScriptDebugServer(editor.GetPlatform().GetProcessId()), "an external debugger can reserve the engine channel");
        const UInt64 beforeBusyStart = editor.GetScriptModule()->GetGeneration();
        editor.StartDebugSession();
        Check(editor.IsScriptDebugServerActive() && false == editor.GetSourceDebugger().IsActive()
            && editor.GetScriptModule()->GetGeneration() == beforeBusyStart && false == editor.IsSimulationPlaying(),
            "native debugging preserves a channel already owned by an external debugger");
        editor.StopScriptDebugServer();

        probe.hits = 0;
        probe.terminateAdapter = true;
        probe.summaryEdit = PrepareSummaryEdit(editor, *probe.document);
        editor.StartDebugSession();
        Pump(editor, [&]
        {
            return probe.hits > 0 && false == editor.IsDebugSessionActive()
                && false == editor.IsScriptDebugServerActive() && false == editor.IsSimulationPlaying();
        }, std::chrono::seconds(120), "adapter death while stopped releases the engine break loop and channel");
        Check(probe.adapterTerminated && editor.GetSourceDebugger().Client().GetState() == Code::DebugState::Failed
            && CallbackCount(editor) == 0.0f,
            "the owned adapter actually died and cleanup restored the pre-play canvas");
        const String adapterFailure = Loc::Text(LocKeys::DebugClientStopped);
        Check(editor.GetSourceDebugger().Client().GetError() == adapterFailure
            && !adapterFailure.Contains("debug_client."), "adapter termination produces the localized client error");
        Pump(editor, [&] {
            const auto& notifications = editor.GetNotifications();
            for (UInt32 index = 0; index < notifications.GetVisibleCount(); ++index)
            {
                const auto notification = notifications.GetVisible(index);
                if (notification.message != nullptr && adapterFailure == notification.message)
                {
                    return true;
                }
            }
            return false;
        }, std::chrono::seconds(10), "the actual engine error notification displays the localized adapter failure");
        probe.terminateAdapter = false;
        const UInt32 afterAdapterDeath = probe.hits;
        Check(editor.StartSimulation() && editor.Tick(Frame) && CallbackCount(editor) == 1.0f && probe.hits == afterAdapterDeath,
            "ordinary simulation remains usable after an adapter dies at a breakpoint");
        editor.StopSimulation();
        TestActualInstallFailureStopsBeforePlay(editor, probe);
        editor.SetScriptBreakFrameHook(nullptr, nullptr);
        editor.CloseProject();
        std::cout << "  SourceDebugger native JBroScript first-callback relocation, mismatch, reload, disconnect, busy-channel, adapter-death and actual install-failure cleanup verified\n";
    }
}

JBro::Int32 RunSourceDebuggerTests()
{
    TestNativeSourceDebugger();
    return 0;
}
