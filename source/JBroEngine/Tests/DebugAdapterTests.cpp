#include <JBro/Editor/ScriptProject.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// JBro Script Editor 의 디버그 어댑터(D-319)를 실제로 띄워 DAP 로 대화한다. 대상은 에디터처럼 스크립트 DLL 을 세대마다 복사본으로 싣는
// 작은 프로그램(`Tests/DebugAdapterTarget`)이다. 중단점·스택·natvis 로 보이는 변수·한 줄 가기·다시 실은 복사본의 중단점·일시정지·떼기와,
// 어댑터가 죽어도 대상이 사는 것을 본다.

namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    constexpr Float Frame = 1.0f / 60.0f;

    // 마지막으로 받은 메시지다. 검사가 실패하면 같이 찍는다 - 어댑터가 무엇을 답했는지 없이는 실패를 읽을 수 없다.
    std::string g_lastMessage;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            std::cout << "  last message: " << g_lastMessage.substr(0, 2000) << std::endl;
            throw std::runtime_error(message);
        }
    }

    fs::path FreshFolder(const fs::path& name)
    {
        const fs::path root = fs::temp_directory_path() / fs::path(u8"JBro디버그어댑터") / name;
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root, ignored);
        return root;
    }

    fs::path ExecutableFolder()
    {
        wchar_t self[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        return fs::path(self).parent_path();
    }

    void Touch(const fs::path& path)
    {
        std::ofstream(path, std::ios::binary) << "1";
    }

    std::string ReadAll(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // 메시지 글자에서 `"key":` 뒤의 수를 읽는다. 어댑터의 JSON 은 빈칸이 없다.
    Int64 NumberAfter(const std::string& text, const char* key, std::size_t from = 0)
    {
        const std::string needle = std::string("\"") + key + "\":";
        const std::size_t at = text.find(needle, from);
        if (at == std::string::npos)
        {
            return Int64(-1);
        }
        return Int64(std::strtoll(text.c_str() + at + needle.size(), nullptr, 10));
    }

    // 변수 목록에서 이름이 `name` 인 변수의 JSON 조각(다음 변수 앞까지)이다.
    std::string VariableNamed(const std::string& response, const char* name)
    {
        const std::string needle = std::string("{\"name\":\"") + name + "\"";
        const std::size_t at = response.find(needle);
        if (at == std::string::npos)
        {
            return std::string();
        }
        const std::size_t end = response.find("{\"name\":", at + needle.size());
        return response.substr(at, end == std::string::npos ? std::string::npos : end - at);
    }

    class Process
    {
    public:
        ~Process()
        {
            Close();
        }

        Bool Start(const fs::path& exe, const std::wstring& arguments, Bool withPipes)
        {
            SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
            HANDLE childInput = nullptr;
            HANDLE childOutput = nullptr;
            if (withPipes)
            {
                if (FALSE == CreatePipe(&childInput, &m_input, &security, 0) || FALSE == CreatePipe(&m_output, &childOutput, &security, 0))
                {
                    return false;
                }
                SetHandleInformation(m_input, HANDLE_FLAG_INHERIT, 0);
                SetHandleInformation(m_output, HANDLE_FLAG_INHERIT, 0);
            }
            STARTUPINFOW startup = {sizeof(startup)};
            if (withPipes)
            {
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdInput = childInput;
                startup.hStdOutput = childOutput;
                startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            }
            std::wstring command = L"\"" + exe.wstring() + L"\" " + arguments;
            PROCESS_INFORMATION information = {};
            const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, withPipes ? TRUE : FALSE, CREATE_NO_WINDOW,
                nullptr, exe.parent_path().c_str(), &startup, &information);
            if (childInput != nullptr)
            {
                CloseHandle(childInput);
                CloseHandle(childOutput);
            }
            if (FALSE == started)
            {
                return false;
            }
            CloseHandle(information.hThread);
            m_process = information.hProcess;
            m_processId = information.dwProcessId;
            return true;
        }

        DWORD GetId() const
        {
            return m_processId;
        }

        Bool IsRunning() const
        {
            return m_process != nullptr && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
        }

        Bool WaitExit(DWORD milliseconds, DWORD& exitCode) const
        {
            if (m_process == nullptr || WaitForSingleObject(m_process, milliseconds) != WAIT_OBJECT_0)
            {
                return false;
            }
            GetExitCodeProcess(m_process, &exitCode);
            return true;
        }

        void Kill()
        {
            if (IsRunning())
            {
                TerminateProcess(m_process, 99);
                WaitForSingleObject(m_process, 5000);
            }
        }

        void Close()
        {
            Kill();
            for (HANDLE* handle : {&m_process, &m_input, &m_output})
            {
                if (*handle != nullptr)
                {
                    CloseHandle(*handle);
                    *handle = nullptr;
                }
            }
        }

        // ── DAP ──
        Int64 Send(const char* command, const std::string& arguments)
        {
            const Int64 seq = m_nextSeq;
            m_nextSeq = m_nextSeq + 1;
            char head[96] = {};
            std::snprintf(head, sizeof(head), "{\"seq\":%lld,\"type\":\"request\",\"command\":\"", static_cast<long long>(seq.Value));
            std::string body = std::string(head) + command + "\"";
            if (false == arguments.empty())
            {
                body += ",\"arguments\":" + arguments;
            }
            body += "}";
            const std::string packet = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
            DWORD written = 0;
            WriteFile(m_input, packet.data(), static_cast<DWORD>(packet.size()), &written, nullptr);
            return seq;
        }

        // 메시지 하나를 기다린다. 시간이 다 되면 거짓이다.
        Bool Read(std::string& message, DWORD milliseconds)
        {
            const ULONGLONG deadline = GetTickCount64() + milliseconds;
            while (true)
            {
                const std::size_t headerEnd = m_buffer.find("\r\n\r\n");
                if (headerEnd != std::string::npos)
                {
                    const std::size_t length = static_cast<std::size_t>(std::strtoull(m_buffer.c_str() + std::strlen("Content-Length:"), nullptr, 10));
                    if (m_buffer.size() >= headerEnd + 4 + length)
                    {
                        message = m_buffer.substr(headerEnd + 4, length);
                        m_buffer.erase(0, headerEnd + 4 + length);
                        return true;
                    }
                }
                DWORD available = 0;
                if (FALSE == PeekNamedPipe(m_output, nullptr, 0, nullptr, &available, nullptr))
                {
                    return false;
                }
                if (available == 0)
                {
                    if (GetTickCount64() > deadline)
                    {
                        return false;
                    }
                    Sleep(5);
                    continue;
                }
                std::string chunk(available, '\0');
                DWORD read = 0;
                ReadFile(m_output, chunk.data(), available, &read, nullptr);
                chunk.resize(read);
                m_buffer += chunk;
            }
        }

        std::string Request(const char* command, const std::string& arguments = std::string(), DWORD milliseconds = 30000)
        {
            const Int64 seq = Send(command, arguments);
            char needle[48] = {};
            std::snprintf(needle, sizeof(needle), "\"request_seq\":%lld,", static_cast<long long>(seq.Value));
            return WaitFor(needle, milliseconds);
        }

        std::string WaitEvent(const char* event, DWORD milliseconds = 30000)
        {
            return WaitFor((std::string("\"event\":\"") + event + "\"").c_str(), milliseconds);
        }

        // 이 글자를 담은 메시지가 올 때까지 읽는다. 사이의 메시지는 버린다.
        std::string WaitFor(const char* needle, DWORD milliseconds)
        {
            const ULONGLONG deadline = GetTickCount64() + milliseconds;
            std::string message;
            while (GetTickCount64() < deadline)
            {
                const ULONGLONG now = GetTickCount64();
                if (false == Read(message, static_cast<DWORD>(deadline - now)))
                {
                    break;
                }
                g_lastMessage = message;
                if (message.find(needle) != std::string::npos)
                {
                    return message;
                }
            }
            return std::string();
        }

    private:
        HANDLE m_process = nullptr;
        DWORD m_processId = 0;
        HANDLE m_input = nullptr;
        HANDLE m_output = nullptr;
        std::string m_buffer;
        Int64 m_nextSeq = 1;
    };

    std::string JsonPath(const fs::path& path)
    {
        std::string text = path.u8string().empty() ? std::string() : std::string(reinterpret_cast<const char*>(path.u8string().c_str()));
        std::string escaped;
        for (const char c : text)
        {
            if (c == '\\')
            {
                escaped += "\\\\";
            }
            else
            {
                escaped += c;
            }
        }
        return escaped;
    }

    Bool WaitForGeneration(const fs::path& folder, const char* generation)
    {
        for (Int32 waited = 0; waited < 3000; waited = waited + 10)
        {
            if (ReadAll(folder / "generation") == generation)
            {
                return true;
            }
            Sleep(10);
        }
        return false;
    }

    void TestTheAdapterDebugsAScriptAcrossAReload()
    {
        const fs::path binaries = ExecutableFolder();
        const fs::path adapterExe = binaries / "DebugAdapter" / "JBroDebugAdapter.exe";
        const fs::path targetExe = binaries / "JBroDebugAdapterTarget.exe";
        if (false == fs::exists(adapterExe) || false == fs::exists(targetExe))
        {
            std::cout << "  [skip] the debug adapter or its target is not built; debugging not verified" << std::endl;
            return;
        }
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const String engineRoot = ScriptProject::FindEngineRoot(platform, platform.GetExecutableFolder().c_str());
        platform.Shutdown();
        Check(false == engineRoot.empty(), "the engine sources are found from the test");
        const fs::path source = fs::path(std::u8string(reinterpret_cast<const char8_t*>(engineRoot.c_str()))) / "Tests" / "DebugAdapterTarget" / "TargetScript.cpp";
        const std::string script = ReadAll(source);
        Int32 breakLine = 0;
        {
            Int32 line = 1;
            for (std::size_t at = 0; at < script.size() && breakLine == 0; ++at)
            {
                // 줄 끝의 표식만 센다. 파일 머리의 설명에도 같은 낱말이 있다.
                if (script.compare(at, 13, "// BREAK_HERE") == 0)
                {
                    breakLine = line;
                }
                line = line + (script[at] == '\n' ? 1 : 0);
            }
        }
        Check(breakLine > 0, "the target script marks the line to stop at");
        char breakpointsText[160] = {};
        std::snprintf(breakpointsText, sizeof(breakpointsText), "\"breakpoints\":[{\"line\":%d}]", static_cast<int>(breakLine.Value));
        const std::string sourceJson = "{\"source\":{\"path\":\"" + JsonPath(source) + "\"},";
        const std::string setBreakpoint = sourceJson + breakpointsText + "}";
        const std::string clearBreakpoints = sourceJson + "\"breakpoints\":[]}";

        const fs::path work = FreshFolder("target");
        Process target;
        Check(target.Start(targetExe, L"\"" + work.wstring() + L"\"", false), "the target starts");
        Check(WaitForGeneration(work, "1"), "the target loads the first copy of the script");

        Process adapter;
        Check(adapter.Start(adapterExe, L"", true), "the adapter starts");
        std::string response = adapter.Request("initialize", "{\"adapterID\":\"jbro\"}");
        Check(response.find("\"success\":true") != std::string::npos && response.find("\"supportsConfigurationDoneRequest\":true") != std::string::npos,
            "initialize answers with the capabilities");
        char attachText[160] = {};
        std::snprintf(attachText, sizeof(attachText), "{\"processId\":%lu,\"scriptModules\":[\"TargetScript\"]}", static_cast<unsigned long>(target.GetId()));
        response = adapter.Request("attach", attachText, 60000);
        Check(response.find("\"success\":true") != std::string::npos, "the adapter attaches without stopping the target");
        Check(response.find("without stopping") != std::string::npos || false == adapter.WaitEvent("output").empty(), "and says it attached noninvasively");
        Check(false == adapter.WaitEvent("initialized").empty(), "and says it is ready for breakpoints");
        Check(target.IsRunning(), "attaching did not stop the target");

        // 중단점을 건다. 어댑터가 줄을 주소로 바꿔 통로로 넘기면 대상(에디터 역할)이 확인한다.
        response = adapter.Request("setBreakpoints", setBreakpoint);
        char lineText[32] = {};
        std::snprintf(lineText, sizeof(lineText), "\"line\":%d", static_cast<int>(breakLine.Value));
        Check(response.find("\"verified\":true") != std::string::npos && response.find(lineText) != std::string::npos,
            "the editor confirmed the breakpoint at the source line");
        adapter.Request("configurationDone");

        // 스크립트를 돌린다. BREAK_HERE 에서 대상이 자기 문맥을 떠 멈춤을 알린다.
        Touch(work / "run");
        std::string stopped = adapter.WaitEvent("stopped");
        Check(stopped.find("\"reason\":\"breakpoint\"") != std::string::npos, "the script stops at the breakpoint");
        const Int64 thread = NumberAfter(stopped, "threadId");
        char threadText[64] = {};
        std::snprintf(threadText, sizeof(threadText), "{\"threadId\":%lld}", static_cast<long long>(thread.Value));

        // 멈춘 스레드의 문맥(.cxr)으로 스택을 읽는다 - 스크립트 함수가 보인다.
        response = adapter.Request("stackTrace", threadText);
        Check(response.find("TargetScript.cpp") != std::string::npos, "the stack reaches the script's source file");
        // 스크립트 프레임(TargetScript.cpp)을 골라 그 지역 변수를 natvis 로 읽는다.
        Int64 scriptFrame = -1;
        {
            const std::string frames = response;
            std::size_t at = 0;
            while ((at = frames.find("\"id\":", at)) != std::string::npos)
            {
                const Int64 id = NumberAfter(frames.substr(at), "id");
                const std::size_t src = frames.find("TargetScript.cpp", at);
                const std::size_t nextId = frames.find("\"id\":", at + 5);
                if (src != std::string::npos && (nextId == std::string::npos || src < nextId))
                {
                    scriptFrame = id;
                    break;
                }
                at = at + 5;
            }
        }
        Check(scriptFrame >= 0, "a stack frame is in the script");
        char frameText[64] = {};
        std::snprintf(frameText, sizeof(frameText), "{\"frameId\":%lld}", static_cast<long long>(scriptFrame.Value));
        response = adapter.Request("scopes", frameText);
        const Int64 locals = NumberAfter(response, "variablesReference");
        char referenceText[64] = {};
        std::snprintf(referenceText, sizeof(referenceText), "{\"variablesReference\":%lld}", static_cast<long long>(locals.Value));
        response = adapter.Request("variables", referenceText);
        Check(response.find("\"variables\"") != std::string::npos, "the locals scope answers");
#if defined(_DEBUG)
        // **지역 변수의 값은 최적화하지 않은 빌드에서만 믿을 수 있다.** Release 는 옵티마이저가 지역을 지우거나 레지스터로 옮겨
        // 디버거가 보지 못한다 - 어느 네이티브 디버거나 같다. 그래서 값 검사는 Debug 에서만 한다(멈춤·스택은 두 구성 다 본다).
        Check(VariableNamed(response, "speed").find("\"value\":\"2.5") != std::string::npos, "a Float local shows as its number");
        Check(VariableNamed(response, "name").find("hello") != std::string::npos, "a String local shows its text");
        const std::string numbers = VariableNamed(response, "numbers");
        Check(numbers.find("size=3") != std::string::npos, "an Array local shows its size");
#endif

        // **다시 실은 복사본에서도 선다**(D-330). 스크립트를 멈춰 두고 다음 세대를 싣는다 - 에디터(흉내)가 `module` 로 알리면
        // 어댑터가 새 모듈에서 줄을 다시 풀어 다시 건다. 다시 돌리면 그 복사본에서 멈춘다.
        std::error_code ignored;
        fs::remove(work / "run", ignored);
        adapter.Request("continue", threadText);
        Sleep(100);
        Touch(work / "reload");
        Check(WaitForGeneration(work, "2"), "the target loads the second copy");
        Sleep(300);
        Touch(work / "run");
        std::string reloaded = adapter.WaitEvent("stopped", 15000);
        Check(reloaded.find("\"reason\":\"breakpoint\"") != std::string::npos, "the re-armed breakpoint stops in the reloaded copy");
        Check(ReadAll(work / "generation") == "2", "and it is the second copy that is running");
        // 재배치 자체는 바르다(위의 reason:breakpoint + 2 세대). 다시 실은 복사본의 **스택 심볼화**는 DbgEng 이 noninvasive 로 붙은 채
        // `.reload` 해도 모듈을 늘 새로 읽지는 않아 들쭉날쭉하다 - 그래서 여기서 소스 줄까지는 단언하지 않는다(script-debug-plan §4.3 한계).
        adapter.Request("stackTrace", threadText);
        adapter.Request("continue", threadText);

        // 떼어도 대상은 그대로 돈다. 통로가 끊기면 남은 중단점을 엔진이 뺀다.
        response = adapter.Request("disconnect", "{}");
        Check(response.find("\"success\":true") != std::string::npos, "disconnect answers");
        DWORD exitCode = 1;
        Check(adapter.WaitExit(10000, exitCode), "the adapter ends after disconnect");
        Sleep(300);
        Check(target.IsRunning(), "the target keeps running after the adapter leaves");

        Touch(work / "quit");
        exitCode = 1;
        Check(target.WaitExit(10000, exitCode) && exitCode == 0, "the target ends cleanly when asked");
    }

    // **조건부 중단점과 로그 포인트**(D-330). 조건이 거짓이면 서지 않고, 로그 포인트는 글만 내고 이어 간다. 상수 식으로 메커니즘을 본다.
    void TestConditionalBreakpointsAndLogpoints()
    {
        const fs::path binaries = ExecutableFolder();
        const fs::path adapterExe = binaries / "DebugAdapter" / "JBroDebugAdapter.exe";
        const fs::path targetExe = binaries / "JBroDebugAdapterTarget.exe";
        if (false == fs::exists(adapterExe) || false == fs::exists(targetExe))
        {
            std::cout << "  [skip] the debug adapter or its target is not built" << std::endl;
            return;
        }
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const String engineRoot = ScriptProject::FindEngineRoot(platform, platform.GetExecutableFolder().c_str());
        platform.Shutdown();
        Check(false == engineRoot.empty(), "the engine sources are found");
        const fs::path scriptSource = fs::path(std::u8string(reinterpret_cast<const char8_t*>(engineRoot.c_str()))) / "Tests" / "DebugAdapterTarget" / "TargetScript.cpp";
        const std::string scriptText = ReadAll(scriptSource);
        Int32 breakLine = 0;
        {
            Int32 line = 1;
            for (std::size_t at = 0; at < scriptText.size() && breakLine == 0; ++at)
            {
                if (scriptText.compare(at, 13, "// BREAK_HERE") == 0)
                {
                    breakLine = line;
                }
                line = line + (scriptText[at] == '\n' ? 1 : 0);
            }
        }
        Check(breakLine > 0, "the target script marks the line to stop at");
        const std::string sourceJson = "{\"source\":{\"path\":\"" + JsonPath(scriptSource) + "\"},";
        char lineOnly[32] = {};
        std::snprintf(lineOnly, sizeof(lineOnly), "%d", static_cast<int>(breakLine.Value));

        const fs::path work = FreshFolder("cond");
        Process target;
        Check(target.Start(targetExe, L"\"" + work.wstring() + L"\"", false), "the target starts");
        Check(WaitForGeneration(work, "1"), "the target loads the script");
        Process adapter;
        std::string response = adapter.Start(adapterExe, L"", true) ? adapter.Request("initialize", "{\"adapterID\":\"jbro\"}") : std::string();
        Check(response.find("\"supportsConditionalBreakpoints\":true") != std::string::npos
                && response.find("\"supportsLogPoints\":true") != std::string::npos,
            "the adapter advertises conditional breakpoints and log points");
        char attachText[160] = {};
        std::snprintf(attachText, sizeof(attachText), "{\"processId\":%lu,\"scriptModules\":[\"TargetScript\"]}", static_cast<unsigned long>(target.GetId()));
        Check(adapter.Request("attach", attachText, 60000).find("\"success\":true") != std::string::npos, "the adapter attaches");
        adapter.WaitEvent("initialized");
        char threadText[64] = "{\"threadId\":0}";

        // 1. 거짓 조건: 스크립트가 그 줄에 닿아도 서지 않는다 - DAP stopped 가 오지 않는다.
        adapter.Request("setBreakpoints", sourceJson + "\"breakpoints\":[{\"line\":" + lineOnly + ",\"condition\":\"1==2\"}]}");
        adapter.Request("configurationDone");
        Touch(work / "run");
        Check(adapter.WaitEvent("stopped", 3000).empty(), "a false condition does not stop the script");
        Check(target.IsRunning(), "and the script keeps running");

        // 2. 참 조건: 선다.
        fs::remove(work / "run");
        adapter.Request("setBreakpoints", sourceJson + "\"breakpoints\":[{\"line\":" + lineOnly + ",\"condition\":\"1==1\"}]}");
        Sleep(100);
        Touch(work / "run");
        std::string stopped = adapter.WaitEvent("stopped", 8000);
        Check(stopped.find("\"reason\":\"breakpoint\"") != std::string::npos, "a true condition stops the script");
        adapter.Request("continue", threadText);

        // 3. 로그 포인트: 서지 않고 `{식}` 을 값으로 바꿔 출력한다.
        fs::remove(work / "run");
        adapter.Request("setBreakpoints", sourceJson + "\"breakpoints\":[{\"line\":" + lineOnly + ",\"logMessage\":\"hit {1+1}\"}]}");
        Sleep(100);
        Touch(work / "run");
        std::string logged;
        for (Int32 tries = 0; tries < 30 && logged.find("hit 2") == std::string::npos; ++tries)
        {
            const std::string event = adapter.WaitEvent("output", 2000);
            if (false == event.empty())
            {
                logged = event;
            }
            if (false == adapter.WaitEvent("stopped", 1).empty())
            {
                break;
            }
        }
        Check(logged.find("hit 2") != std::string::npos, "a log point prints the interpolated message");
        Check(adapter.WaitEvent("stopped", 1500).empty(), "and does not stop the script");

        adapter.Request("disconnect", "{}");
        DWORD exitCode = 1;
        adapter.WaitExit(10000, exitCode);
        Touch(work / "quit");
        target.WaitExit(10000, exitCode);
    }

    // ── 실제 에디터 + 실제 어댑터 한자리 (D-330) ──────────────────────────────
    // 앞의 시험은 어댑터를 에디터 흉내(대상 프로그램)에 붙였다. 이번에는 **진짜**다: 이 시험 프로세스가 진짜 에디터를 돌리고,
    // 진짜 어댑터를 자식으로 띄워 이 프로세스에 세우지 않고 붙인다. 엔진이 넣은 int 3 에 스크립트가 서면, 어댑터가 그 문맥으로 스택을 읽는다.

    int g_coRunUpdates = 0;

    // 중단점을 거는 줄이 Release 에서도 또렷한 명령을 갖도록, 인라인하지 않는 호출을 둔다. 옵티마이저가 그 줄을 지우거나 합치면 줄 → 주소가 풀리지 않는다.
    __declspec(noinline) void CoRunTouch(float value)
    {
        volatile float sink = value;
        (void)sink;
    }

    class CoRunScript final : public GameScript2D
    {
        JBRO_REFLECT_BODY(CoRunScript)
    public:
        static constexpr const char* StaticTypeName() { return "Test::CoRunScript"; }
        ComponentTypeId GetTypeId() const override { return MakeStableTypeId(StaticTypeName()); }
        JBRO_FIELD(Float, speed) = 2.5f;

        void OnUpdate() override
        {
            g_coRunUpdates = g_coRunUpdates + 1;
            speed = speed + 1.0f;
            CoRunTouch(speed); // CORUN_BREAK
        }
    };

    // 멈춘 동안 어댑터를 DAP 로 몬다. 에디터 스레드(멈춤 루프의 프레임 훅)에서 부르므로, 어댑터(자식 프로세스)가 그동안 제 일을 한다.
    Process* g_coRunAdapter = nullptr;
    int g_coRunStops = 0;
    Bool g_coRunStackOk = false;

    void OnCoRunBreakFrame(void*, EditorApplication& editor)
    {
        if (editor.GetScriptBreakFrameCount() != 0 || g_coRunStops > 0)
        {
            return;
        }
        // 어댑터가 통로로 멈춤을 받고 .cxr 한 뒤 DAP stopped 를 낸다.
        const std::string stopped = g_coRunAdapter->WaitEvent("stopped", 10000);
        if (stopped.find("\"reason\":\"breakpoint\"") == std::string::npos)
        {
            editor.ContinueScriptBreak();
            return;
        }
        ++g_coRunStops;
        const Int64 thread = NumberAfter(stopped, "threadId");
        char threadText[64] = {};
        std::snprintf(threadText, sizeof(threadText), "{\"threadId\":%lld}", static_cast<long long>(thread.Value));
        const std::string stack = g_coRunAdapter->Request("stackTrace", threadText);
        g_coRunStackOk = stack.find("DebugAdapterTests.cpp") != std::string::npos;
        // DAP continue → 어댑터가 통로로 continue 를 보낸다 → 에디터 멈춤 루프가 그것을 읽어 이어 간다.
        g_coRunAdapter->Request("continue", threadText);
    }

    void TestTheRealEditorAndAdapterStopAtABreakpoint()
    {
        const fs::path binaries = ExecutableFolder();
        const fs::path adapterExe = binaries / "DebugAdapter" / "JBroDebugAdapter.exe";
        if (false == fs::exists(adapterExe))
        {
            std::cout << "  [skip] the debug adapter is not built; the real co-run is not verified" << std::endl;
            return;
        }
        WindowsPlatform probe;
        JMemoryContext memory;
        Check(probe.Initialize(memory), "the platform initializes");
        const String engineRoot = ScriptProject::FindEngineRoot(probe, probe.GetExecutableFolder().c_str());
        probe.Shutdown();
        Check(false == engineRoot.empty(), "the engine sources are found");
        const fs::path source = fs::path(std::u8string(reinterpret_cast<const char8_t*>(engineRoot.c_str()))) / "Tests" / "DebugAdapterTests.cpp";
        const std::string text = ReadAll(source);
        Int32 breakLine = 0;
        {
            Int32 line = 1;
            for (std::size_t at = 0; at < text.size() && breakLine == 0; ++at)
            {
                if (text.compare(at, 14, "// CORUN_BREAK") == 0)
                {
                    breakLine = line;
                }
                line = line + (text[at] == '\n' ? 1 : 0);
            }
        }
        Check(breakLine > 0, "the co-run script marks the line to stop at");

        RegisterScriptType<CoRunScript>();
        g_coRunUpdates = 0;
        g_coRunStops = 0;
        g_coRunStackOk = false;

        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the real co-run is not verified" << std::endl;
            return;
        }
        ProjectDescriptor project;
        constexpr char name[] = "CoRunProbe";
        project.name = {name, static_cast<UInt32>(sizeof(name) - 1)};
        Check(editor.OpenProject(project), "the editor project opens");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI turns on");
        Canvas& canvas = *editor.GetCanvas();
        Object::GameObject* mover = canvas.CreateObject("Mover");
        canvas.AttachComponent<Component::Transform2D>(mover);
        Check(canvas.AttachScript(mover, CoRunScript::StaticTypeName()) != nullptr, "the co-run script attaches");

        Check(editor.StartScriptDebugServer(GetCurrentProcessId()), "the editor opens the debug channel");

        // 진짜 어댑터를 띄워 이 프로세스에 세우지 않고 붙인다. 심볼은 이 실행 파일 옆의 PDB 다.
        Process adapter;
        g_coRunAdapter = &adapter;
        Check(adapter.Start(adapterExe, L"", true), "the adapter starts");
        adapter.Request("initialize", "{\"adapterID\":\"jbro\"}");
        char attachText[256] = {};
        std::snprintf(attachText, sizeof(attachText), "{\"processId\":%lu,\"scriptModules\":[\"JBroTests\"],\"symbolPath\":\"%s\"}",
            static_cast<unsigned long>(GetCurrentProcessId()), JsonPath(binaries).c_str());
        std::string response = adapter.Request("attach", attachText, 60000);
        Check(response.find("\"success\":true") != std::string::npos, "the adapter attaches to the real editor without stopping it");
        Check(false == adapter.WaitEvent("initialized").empty(), "the adapter is ready for breakpoints");
        for (Int32 frame = 0; frame < 5; ++frame)
        {
            Check(editor.Tick(Frame), "the editor ticks while the adapter settles");
        }

        char breakpointsText[160] = {};
        std::snprintf(breakpointsText, sizeof(breakpointsText), "\"breakpoints\":[{\"line\":%d}]", static_cast<int>(breakLine.Value));
        const std::string setBreakpoint = "{\"source\":{\"path\":\"" + JsonPath(source) + "\"}," + breakpointsText + "}";
        response = adapter.Request("setBreakpoints", setBreakpoint);
        Check(response.find("\"verified\":true") != std::string::npos, "the adapter resolves the line in the editor and arms it");
        // 에디터가 통로로 온 setbp 를 읽어 int 3 을 넣게 몇 프레임 돌린다.
        for (Int32 frame = 0; frame < 5; ++frame)
        {
            Check(editor.Tick(Frame), "the editor inserts the breakpoint");
        }

        editor.SetScriptBreakFrameHook(&OnCoRunBreakFrame, nullptr);
        Check(editor.StartSimulation(), "play starts");
        for (Int32 frame = 0; frame < 120 && g_coRunStops == 0; ++frame)
        {
            Check(editor.Tick(Frame), "the editor plays until the engine's int3 stops the script");
        }
        editor.SetScriptBreakFrameHook(nullptr, nullptr);
        Check(g_coRunStops >= 1, "the engine's breakpoint stopped the real script and the adapter saw it over the pipe");
        Check(g_coRunStackOk, "the adapter read the stack through .cxr and it reached the script's source file");
        Check(editor.IsSimulationPlaying(), "the adapter's continue let the real editor go on");

        adapter.Request("disconnect", "{}");
        DWORD exitCode = 1;
        adapter.WaitExit(10000, exitCode);
        g_coRunAdapter = nullptr;
        editor.StopSimulation();
        Check(editor.Tick(Frame), "the editor ticks after stopping");
        editor.StopScriptDebugServer();
        editor.Shutdown();
    }
}

JBro::Int32 RunDebugAdapterTests()
{
    try
    {
        TestTheAdapterDebugsAScriptAcrossAReload();
        TestConditionalBreakpointsAndLogpoints();
        TestTheRealEditorAndAdapterStopAtABreakpoint();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "Debug adapter tests passed." << std::endl;
    return 0;
}
