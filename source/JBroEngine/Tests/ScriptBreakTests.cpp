#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorNotifications.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptCallGuard.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <cstdint>
#include <Windows.h>
#include <cstring>
#include <string>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **스크립트가 중단점에 서도 에디터는 선다 - 화면은 돌고 편집만 막힌다**(D-330 2 단계). 시험이 등록한 스크립트를 에디터의 진짜
// `ScriptSystem` 으로 돌리며 스크립트의 `__debugbreak` 와 엔진이 넣은 int 3 에서 세운다. 선 동안의 프레임마다 시험이 사람 대신 보고 누른다
// (`SetScriptBreakFrameHook`). 본 것: 선 자리와 그 줄, 선 동안 그린 프레임, 막힌 편집, 이어 가기와 그 뒤의 실행, 정지와 되돌린 캔버스,
// 넣은 중단점이 매 프레임 다시 걸리고 빼면 원래 코드가 돈다는 것, 실행 밖에서 걸린 중단점이 아무 일 없이 지나가는 것.

namespace
{
    using namespace JBro;

    constexpr Float Frame = 1.0f / 60.0f;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    struct BreakControl
    {
        Bool breakInUpdate = false;
        Int32 triggerUpdate = 1;
        Int32 updates = 0;
        Int32 targetCalls = 0;
    };

    BreakControl g_control;

    // 엔진이 중단점을 넣을 함수다. 스크립트가 매 프레임 부른다.
    __declspec(noinline) void BreakTarget()
    {
        ++g_control.targetCalls;
    }

    class BreakScript final : public GameScript2D
    {
        JBRO_REFLECT_BODY(BreakScript)
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::BreakScript";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_FIELD(Float, speed) = 1.0f;

        void OnUpdate() override
        {
            ++g_control.updates;
            speed = speed + 1.0f;
            BreakTarget();
            if (g_control.breakInUpdate && g_control.updates == g_control.triggerUpdate)
            {
                __debugbreak();
                speed = speed + 2.0f;
                speed = speed + 4.0f;
                g_control.targetCalls = g_control.targetCalls + 1;
            }
        }
    };

    String CanvasText(Canvas& canvas)
    {
        String text;
        CanvasFileError error;
        Check(WriteCanvasText(canvas, text, error), "the canvas must be writable as text");
        return text;
    }

    Bool Logged(const char* needle)
    {
        const std::size_t count = Log::GetCount();
        for (std::size_t index = 0; index < count; ++index)
        {
            const LogEntry* entry = Log::GetAt(index);
            if (entry != nullptr && std::strstr(entry->message, needle) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    // 증분 링크면 함수 주소가 점프 칸(`jmp rel32`)이다. 엔진이 넣는 중단점은 함수 본문에 있어야 하니 그 칸을 따라간다.
    UInt64 CodeAddressOf(void (*function)())
    {
        auto* code = reinterpret_cast<const std::uint8_t*>(function);
        if (code[0] == 0xE9)
        {
            std::int32_t relative = 0;
            std::memcpy(&relative, code + 1, sizeof(relative));
            code = code + 5 + relative;
        }
        return UInt64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(code)));
    }

    // 선 동안 시험이 하는 일이다.
    enum class BreakAction
    {
        Continue,
        Stop,
        // 세 번째로 설 때 넣은 중단점을 빼고 이어 간다.
        ClearOnThirdThenContinue,
    };

    struct BreakProbe
    {
        BreakAction action = BreakAction::Continue;
        Int32 framesBeforeAction = 20;
        Int32 hits = 0;
        Int32 framesSeen = 0;
        UInt64 breakpoint = 0;
        Bool sawPaused = true;
        Bool editRefused = true;
        Bool sawLocation = true;
        Bool notified = true;
        const char* expectFunction = nullptr;
        Object::GameObject* subject = nullptr;
        UInt32 lastFrameCount = 0;
        Bool framesAdvanced = true;
    };

    BreakProbe g_probe;

    // 이 스레드 스택의 바닥부터 지금 커밋된 끝까지다. 커밋은 줄지 않으므로 지금까지 가장 깊이 쓴 양이다.
    std::size_t CommittedStackBytes()
    {
        ULONG_PTR low = 0;
        ULONG_PTR high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        // 커밋된 스택은 속성마다 여러 영역으로 나뉜다. 위에서부터 지킴 페이지나 커밋 안 된 곳을 만날 때까지 내려간다.
        ULONG_PTR bottom = high;
        MEMORY_BASIC_INFORMATION info = {};
        while (bottom > low && VirtualQuery(reinterpret_cast<const void*>(bottom - 1), &info, sizeof(info)) != 0
            && info.State == MEM_COMMIT && (info.Protect & PAGE_GUARD) == 0)
        {
            bottom = reinterpret_cast<ULONG_PTR>(info.BaseAddress);
        }
        return static_cast<std::size_t>(high - bottom);
    }

    void OnBreakFrame(void*, EditorApplication& editor)
    {
        BreakProbe& probe = g_probe;
        Check(editor.IsAtScriptBreak(), "the hook only runs while a script is stopped");
        if (editor.GetScriptBreakFrameCount() == 0)
        {
            ++probe.hits;
            probe.lastFrameCount = 0;
            const CodeLocation& location = editor.GetScriptBreakLocation();
            probe.sawLocation = probe.sawLocation && location.line > 0
                && (probe.expectFunction == nullptr || location.function.find(probe.expectFunction) != String::npos)
                && location.file.find("ScriptBreakTests.cpp") != String::npos;
            probe.sawPaused = probe.sawPaused && editor.IsSimulationPaused() && editor.IsSimulationPlaying();
            probe.notified = probe.notified && std::strcmp(editor.GetNotifications().GetLastTitle(),
                Loc::TextOr(LocKeys::NotifyScriptBreak, "A script stopped at a breakpoint")) == 0;
            // 편집은 막힌다. 스크립트 스택이 쥔 캔버스다.
            if (probe.subject != nullptr)
            {
                const EditorObjectId id = editor.GetObjectIds().Track(probe.subject);
                probe.editRefused = probe.editRefused && editor.GetCommands().IsLocked()
                    && false == editor.GetCommands().Execute(MakeOwnerPtr<RenameObjectCommand>(editor.GetObjectIds(), id, "Renamed"));
            }
            // 한 프레임씩 진행은 선 동안 듣지 않는다.
            editor.StepSimulation();
        }
        else
        {
            probe.framesAdvanced = probe.framesAdvanced && editor.GetScriptBreakFrameCount() > probe.lastFrameCount;
            probe.lastFrameCount = editor.GetScriptBreakFrameCount();
        }
        ++probe.framesSeen;
        if (static_cast<std::int32_t>(editor.GetScriptBreakFrameCount().Value) < probe.framesBeforeAction.Value)
        {
            return;
        }
        switch (probe.action)
        {
        case BreakAction::Continue:
            // 도구 모음의 일시정지 풀기와 같은 길이다.
            editor.SetSimulationPaused(false);
            break;
        case BreakAction::Stop:
            editor.StopSimulation();
            break;
        case BreakAction::ClearOnThirdThenContinue:
            if (probe.hits == 3)
            {
                Check(editor.ClearScriptBreakpoint(probe.breakpoint), "the breakpoint comes out while stopped on it");
            }
            editor.ContinueScriptBreak();
            break;
        }
    }

    void TestScriptBreakStopsOnlyTheGame()
    {
        RegisterScriptType<BreakScript>();
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; script breaks not verified" << std::endl;
            return;
        }
        ProjectDescriptor project;
        constexpr char name[] = "ScriptBreakProbe";
        project.name = {name, static_cast<UInt32>(sizeof(name) - 1)};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        editor.SetScriptBreakFrameHook(&OnBreakFrame, nullptr);

        Canvas& canvas = *editor.GetCanvas();
        Object::GameObject* mover = canvas.CreateObject("Mover");
        canvas.AttachComponent<Component::Transform2D>(mover)->position = Vector2{1.0f, 2.0f};
        Check(canvas.AttachScript(mover, BreakScript::StaticTypeName()) != nullptr, "the break script attaches by name");
        Check(editor.Tick(Frame), "the editor must tick before play");
        const String baseline = CanvasText(canvas);

        // 1. 스크립트의 `__debugbreak` 에서 서고, 일시정지를 풀면 그 자리에서 이어 돈다.
        {
            std::cout << "  scenario: __debugbreak, look around, then go on" << std::endl;
            g_control = BreakControl{};
            g_control.breakInUpdate = true;
            g_control.triggerUpdate = 5;
            g_probe = BreakProbe{};
            g_probe.action = BreakAction::Continue;
            g_probe.expectFunction = "OnUpdate";
            g_probe.subject = mover;
            Log::Clear();
            Check(editor.StartSimulation(), "play must start");
            for (Int32 frame = 0; frame < 12; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must tick through the break");
            }
            Check(g_probe.hits == 1, "the script stopped exactly once");
            Check(g_probe.framesSeen > 20 && g_probe.framesAdvanced, "the editor kept drawing while the script stood");
            Check(g_probe.sawPaused, "a stopped script reads as paused play");
            Check(g_probe.sawLocation, "the stop names the script's file, line and function");
            Check(g_probe.notified, "and tells the user");
            Check(g_probe.editRefused, "editing is refused while the script stands");
            // 선 동안의 에디터 프레임은 스크립트 스택 위에 예외 처리기와 함께 얹힌다(D-330 §3.6). 얼마나 깊었는지 남기고, 1 MB 스택의 절반을 넘지 않는지 본다.
            ULONG_PTR low = 0;
            ULONG_PTR high = 0;
            GetCurrentThreadStackLimits(&low, &high);
            const std::size_t committed = CommittedStackBytes();
            std::cout << "  stack committed after the break frames: " << committed / 1024 << " KB of " << (high - low) / 1024 << " KB" << std::endl;
            Check(committed < (high - low) / 2, "the editor frames drawn at a break stay well inside the stack");
            Check(false == editor.IsAtScriptBreak() && false == editor.GetCommands().IsLocked(), "going on unlocks editing");
            Check(editor.IsSimulationPlaying() && false == editor.IsSimulationPaused(), "play goes on after the break");
            Check(g_control.updates >= 12, "and the script runs every frame after it");
            Check(false == ScriptCallGuard::HasFault(), "going on from a break is not a fault");
            editor.StopSimulation();
            Check(editor.Tick(Frame), "the editor must tick after stopping");
            Check(CanvasText(canvas) == baseline, "stopping later restores the canvas");
        }

        // 2. 선 자리에서 정지하면 그 훅을 끝내고 실행 전 캔버스로 돌아간다. 오류가 아니다.
        {
            std::cout << "  scenario: __debugbreak, then stop" << std::endl;
            g_control = BreakControl{};
            g_control.breakInUpdate = true;
            g_control.triggerUpdate = 3;
            g_probe = BreakProbe{};
            g_probe.action = BreakAction::Stop;
            g_probe.framesBeforeAction = 5;
            Log::Clear();
            Check(editor.StartSimulation(), "play must start");
            Int32 frames = 0;
            while (editor.IsSimulationPlaying() && frames < 30)
            {
                Check(editor.Tick(Frame), "the editor must tick through a stop at a break");
                ++frames;
            }
            Check(g_probe.hits == 1, "the script stopped once");
            Check(false == editor.IsSimulationPlaying(), "stopping at a break ends play");
            Check(g_control.updates == 3, "no script hook ran after the stop");
            Check(false == ScriptCallGuard::HasFault(), "the editor took the stopped hook");
            Check(Logged("stopped at a script break"), "the log says why play ended");
            Check(editor.GetNotifications().GetLastLevel() != NotificationLevel::Error, "a stop the user chose is not reported as an error");
            Check(CanvasText(canvas) == baseline, "the canvas is back as it was before play");
            Check(editor.GetCommands().Execute(MakeOwnerPtr<RenameObjectCommand>(editor.GetObjectIds(), editor.GetObjectIds().Track(mover), "Moved"))
                    && editor.GetCommands().Undo(),
                "editing and undo work again after the stop");
        }

        // 3. 엔진이 넣은 중단점: 매 프레임 다시 걸리고, 빼면 원래 코드가 그대로 돈다.
        {
            std::cout << "  scenario: an inserted breakpoint, three hits, then cleared" << std::endl;
            g_control = BreakControl{};
            g_probe = BreakProbe{};
            g_probe.action = BreakAction::ClearOnThirdThenContinue;
            g_probe.framesBeforeAction = 2;
            g_probe.expectFunction = "BreakTarget";
            g_probe.breakpoint = CodeAddressOf(&BreakTarget);
            Check(editor.SetScriptBreakpoint(g_probe.breakpoint), "the engine puts a breakpoint into script code");
            Check(editor.StartSimulation(), "play must start");
            for (Int32 frame = 0; frame < 10; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must tick through inserted breakpoints");
            }
            Check(g_probe.hits == 3, "the breakpoint caught the first three frames and nothing after it came out");
            Check(g_probe.sawLocation, "each stop names the function the breakpoint is in");
            Check(g_control.targetCalls >= 10 && g_control.targetCalls == g_control.updates, "the code under the breakpoint ran every frame, stopped or not");
            Check(false == ScriptCallGuard::HasFault(), "stepping over an inserted breakpoint is not a fault");
            editor.StopSimulation();
            Check(editor.Tick(Frame), "the editor must tick after stopping");
            Check(CanvasText(canvas) == baseline, "the canvas is back as it was");
        }

        // 4. 실행 밖에서(편집 중, 엔진이 스크립트를 부르지 않는 자리) 걸린 중단점은 아무 일 없이 지나간다.
        {
            std::cout << "  scenario: an inserted breakpoint hit outside play" << std::endl;
            g_control = BreakControl{};
            g_probe = BreakProbe{};
            const UInt64 address = CodeAddressOf(&BreakTarget);
            Check(editor.SetScriptBreakpoint(address), "the breakpoint goes in");
            BreakTarget();
            BreakTarget();
            Check(g_control.targetCalls == 2 && g_probe.hits == 0, "a breakpoint outside play does not stop and the code still runs, twice");
            Check(editor.ClearScriptBreakpoint(address), "and comes out");
            BreakTarget();
            Check(g_control.targetCalls == 3, "the original code runs after it comes out");
        }

        editor.SetScriptBreakFrameHook(nullptr, nullptr);
        editor.Shutdown();
    }

    // 파이프 끝에서 어댑터 흉내를 낸다. 에디터와 같은 스레드에서 돌리므로(멈춤 루프의 프레임 훅에서 쓴다) 막히지 않게 읽는다.
    struct PipeClient
    {
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::string buffer;

        Bool Connect(unsigned long pid)
        {
            char name[64] = {};
            std::snprintf(name, sizeof(name), "\\\\.\\pipe\\jbro-debug-%lu", pid);
            const std::wstring wide(name, name + std::strlen(name));
            for (int tries = 0; tries < 200; ++tries)
            {
                handle = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (handle != INVALID_HANDLE_VALUE)
                {
                    return true;
                }
                Sleep(2);
            }
            return false;
        }

        void Write(const std::string& line)
        {
            const std::string packet = line + "\n";
            DWORD written = 0;
            WriteFile(handle, packet.data(), static_cast<DWORD>(packet.size()), &written, nullptr);
        }

        // 와 있는 줄 하나를 꺼낸다(막지 않는다). 없으면 비었다.
        std::string TakeLine()
        {
            for (;;)
            {
                const std::size_t newline = buffer.find('\n');
                if (newline != std::string::npos)
                {
                    std::string line = buffer.substr(0, newline);
                    buffer.erase(0, newline + 1);
                    return line;
                }
                DWORD available = 0;
                if (FALSE == PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                {
                    return std::string();
                }
                std::string chunk(available, '\0');
                DWORD read = 0;
                ReadFile(handle, chunk.data(), available, &read, nullptr);
                buffer.append(chunk.data(), read);
            }
        }

        // 이 글자로 시작하는 줄이 올 때까지 짧게 기다렸다 꺼낸다.
        std::string WaitLine(const char* prefix)
        {
            for (int tries = 0; tries < 400; ++tries)
            {
                const std::string line = TakeLine();
                if (false == line.empty() && line.compare(0, std::strlen(prefix), prefix) == 0)
                {
                    return line;
                }
                if (line.empty())
                {
                    Sleep(2);
                }
            }
            return std::string();
        }

        void Close()
        {
            if (handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
            }
        }
    };

    PipeClient* g_client = nullptr;

    // 멈춘 동안 어댑터가 하는 일: `stopped` 를 읽어 두고 두 프레임 뒤 `continue` 를 보낸다.
    void OnDebugBreakFrame(void*, EditorApplication& editor)
    {
        if (editor.GetScriptBreakFrameCount() == 0)
        {
            const std::string stopped = g_client->WaitLine("stopped");
            Check(false == stopped.empty(), "the adapter receives the stopped event over the pipe");
            // stopped <addr> <ctx> <tid> - 셋 다 0 이 아니어야 한다.
            unsigned long long address = 0;
            unsigned long long context = 0;
            unsigned long thread = 0;
            Check(::sscanf_s(stopped.c_str(), "stopped %llx %llx %lu", &address, &context, &thread) == 3,
                "the stopped event carries the address, context and thread");
            Check(address != 0 && context != 0 && thread != 0, "none of them is empty");
            g_probe.hits = g_probe.hits + 1;
            return;
        }
        if (editor.GetScriptBreakFrameCount() == 2)
        {
            g_client->Write("continue");
        }
    }

    // 한 줄 가기(D-330 4 단계)가 선 자리에서 한 줄씩 나아가는지다. 매 멈춤에서 다음 줄로 넘어가라 한다.
    struct StepProbe
    {
        Int32 stops = 0;
        UInt32 lines[5] = {};
    };
    StepProbe g_stepProbe;

    void OnStepFrame(void*, EditorApplication& editor)
    {
        if (editor.GetScriptBreakFrameCount() != 0)
        {
            return;
        }
        StepProbe& probe = g_stepProbe;
        if (probe.stops < 5)
        {
            probe.lines[probe.stops] = editor.GetScriptBreakLocation().line;
        }
        probe.stops = probe.stops + 1;
        if (probe.stops < 3)
        {
            editor.StepScriptBreak(CodeStepKind::Over);
        }
        else
        {
            editor.SetSimulationPaused(false);
        }
    }

    void TestSteppingAdvancesTheLine()
    {
        RegisterScriptType<BreakScript>();
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; stepping not verified" << std::endl;
            return;
        }
        ProjectDescriptor project;
        constexpr char name[] = "StepProbe";
        project.name = {name, static_cast<UInt32>(sizeof(name) - 1)};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        editor.SetScriptBreakFrameHook(&OnStepFrame, nullptr);

        Canvas& canvas = *editor.GetCanvas();
        Object::GameObject* mover = canvas.CreateObject("Mover");
        canvas.AttachComponent<Component::Transform2D>(mover);
        Check(canvas.AttachScript(mover, BreakScript::StaticTypeName()) != nullptr, "the break script attaches");

        g_control = BreakControl{};
        g_control.breakInUpdate = true;
        g_control.triggerUpdate = 3;
        g_stepProbe = StepProbe{};
        Check(editor.StartSimulation(), "play starts");
        for (Int32 frame = 0; frame < 20 && editor.IsSimulationPlaying() && g_stepProbe.stops < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor ticks through the stepping");
        }
        editor.SetScriptBreakFrameHook(nullptr, nullptr);
        Check(g_stepProbe.stops >= 3, "the script stopped at the break and after two steps");
        Check(g_stepProbe.lines[1] > g_stepProbe.lines[0] && g_stepProbe.lines[2] > g_stepProbe.lines[1],
            "each step over moved to a later source line in the same script");
        Check(false == ScriptCallGuard::HasFault(), "stepping is not a fault");
        editor.StopSimulation();
        Check(editor.Tick(Frame), "the editor ticks after stopping");
        editor.Shutdown();
    }

    // **파이프 통로가 어댑터와 에디터를 잇는다**(D-330 3 단계). 어댑터 흉내가 중단점을 걸고, 멈춤을 받고, 이어 가라 한다.
    void TestTheDebugServerRelaysAcrossThePipe()
    {
        RegisterScriptType<BreakScript>();
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the debug channel is not verified" << std::endl;
            return;
        }
        ProjectDescriptor project;
        constexpr char name[] = "DebugChannelProbe";
        project.name = {name, static_cast<UInt32>(sizeof(name) - 1)};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        Check(editor.StartScriptDebugServer(GetCurrentProcessId()), "the editor opens the debug channel");
        Check(editor.IsScriptDebugServerActive(), "the channel is active");
        PipeClient client;
        g_client = &client;
        Check(client.Connect(GetCurrentProcessId()), "the adapter connects to the channel");

        Canvas& canvas = *editor.GetCanvas();
        Object::GameObject* mover = canvas.CreateObject("Mover");
        canvas.AttachComponent<Component::Transform2D>(mover);
        Check(canvas.AttachScript(mover, BreakScript::StaticTypeName()) != nullptr, "the break script attaches");
        Check(editor.Tick(Frame), "the editor ticks before play");

        // setbp / clearbp 왕복: 주소를 걸고 답을 받는다.
        char setbp[48] = {};
        const UInt64 address = CodeAddressOf(&BreakTarget);
        std::snprintf(setbp, sizeof(setbp), "setbp %llx", static_cast<unsigned long long>(address.Value));
        client.Write(setbp);
        Check(editor.Tick(Frame), "the editor ticks so the server reads the command");
        const std::string reply = client.WaitLine("bp");
        char expected[48] = {};
        std::snprintf(expected, sizeof(expected), "bp %llx 1", static_cast<unsigned long long>(address.Value));
        Check(reply == expected, "the server confirms the breakpoint went in");
        char clearbp[48] = {};
        std::snprintf(clearbp, sizeof(clearbp), "clearbp %llx", static_cast<unsigned long long>(address.Value));
        client.Write(clearbp);
        Check(editor.Tick(Frame), "the editor ticks so the breakpoint comes out");

        // 스크립트의 __debugbreak 에서 선다. 어댑터가 stopped 를 받고 continue 로 이어 가게 한다.
        g_control = BreakControl{};
        g_control.breakInUpdate = true;
        g_control.triggerUpdate = 3;
        g_probe = BreakProbe{};
        editor.SetScriptBreakFrameHook(&OnDebugBreakFrame, nullptr);
        Check(editor.StartSimulation(), "play starts");
        for (Int32 frame = 0; frame < 12; ++frame)
        {
            Check(editor.Tick(Frame), "the editor ticks through the break");
        }
        editor.SetScriptBreakFrameHook(nullptr, nullptr);
        Check(g_probe.hits == 1, "the adapter saw exactly one stop over the pipe");
        Check(false == editor.IsAtScriptBreak() && editor.IsSimulationPlaying(), "the adapter's continue let play go on");
        Check(false == client.WaitLine("resumed").empty(), "the adapter is told the script resumed");

        editor.StopSimulation();
        Check(editor.Tick(Frame), "the editor ticks after stopping");
        client.Close();
        g_client = nullptr;
        editor.StopScriptDebugServer();
        editor.Shutdown();
    }

    // **디버그 실행 메뉴**(D-330 5 단계). 통로를 열고 재생을 시작하며, 어댑터가 붙을 수 있다. 다시 누르면 재생과 통로를 함께 닫는다.
    void TestDebugSessionOpensTheChannelAndPlays()
    {
        RegisterScriptType<BreakScript>();
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the debug menu is not verified" << std::endl;
            return;
        }
        ProjectDescriptor project;
        constexpr char name[] = "DebugMenuProbe";
        project.name = {name, static_cast<UInt32>(sizeof(name) - 1)};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        Canvas& canvas = *editor.GetCanvas();
        canvas.AttachComponent<Component::Transform2D>(canvas.CreateObject("Mover"));

        Check(false == editor.IsDebugSessionActive(), "debugging is off at first");
        editor.StartDebugSession();
        Check(editor.IsDebugSessionActive() && editor.IsScriptDebugServerActive(), "start debugging opens the channel");
        Check(editor.IsSimulationPlaying(), "and starts play so scripts can hit breakpoints");
        Check(editor.GetNotifications().GetLastLevel() == NotificationLevel::Info, "the user is told debugging is on");

        // 어댑터가 붙을 수 있다 - 통로가 이 프로세스의 이름으로 열려 있다.
        PipeClient client;
        Check(client.Connect(GetCurrentProcessId()), "an adapter can connect while debugging");
        Check(editor.Tick(Frame), "the editor ticks while debugging");
        client.Close();

        editor.StopDebugSession();
        Check(false == editor.IsDebugSessionActive() && false == editor.IsScriptDebugServerActive(), "stop debugging closes the channel");
        Check(false == editor.IsSimulationPlaying(), "and stops play");
        editor.Shutdown();
    }
}

JBro::Int32 RunScriptBreakTests()
{
    TestScriptBreakStopsOnlyTheGame();
    TestSteppingAdvancesTheLine();
    TestTheDebugServerRelaysAcrossThePipe();
    TestDebugSessionOpensTheChannelAndPlays();
    std::cout << "ScriptBreak tests passed.\n";
    return 0;
}
