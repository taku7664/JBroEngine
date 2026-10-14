#include <JBro/Editor/ScriptProject.h>
#include <JBro/Platform/WindowsPlatform.h>

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
        Touch(work / "run");
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
        Check(response.find("\"success\":true") != std::string::npos, "the adapter attaches to the running target");
        Check(false == adapter.WaitEvent("initialized").empty(), "and says it is ready for breakpoints");

        response = adapter.Request("setBreakpoints", setBreakpoint);
        char lineText[32] = {};
        std::snprintf(lineText, sizeof(lineText), "\"line\":%d", static_cast<int>(breakLine.Value));
        Check(response.find("\"verified\":true") != std::string::npos && response.find(lineText) != std::string::npos,
            "a source line in the loaded script copy is a verified breakpoint");
        adapter.Request("configurationDone");
        // 붙을 때의 끼어들기를 사용자 멈춤으로 알리면 첫 멈춤이 `exception` 이 된다.
        std::string stopped = adapter.WaitEvent("stopped");
        Check(stopped.find("\"reason\":\"breakpoint\"") != std::string::npos, "the first stop is the breakpoint, not the attach");
        const Int64 thread = NumberAfter(stopped, "threadId");

        char threadText[64] = {};
        std::snprintf(threadText, sizeof(threadText), "{\"threadId\":%lld}", static_cast<long long>(thread.Value));
        response = adapter.Request("stackTrace", threadText);
        Check(response.find("TargetScript.cpp") != std::string::npos && NumberAfter(response, "line") == Int64(breakLine.Value),
            "the top frame is the script line with its source file");
        Check(response.find("JBroDebugAdapterTarget!") != std::string::npos, "and the frames go on into the program that called it");
        const Int64 frame = NumberAfter(response, "id");

        char frameText[64] = {};
        std::snprintf(frameText, sizeof(frameText), "{\"frameId\":%lld}", static_cast<long long>(frame.Value));
        response = adapter.Request("scopes", frameText);
        const Int64 locals = NumberAfter(response, "variablesReference");
        char referenceText[64] = {};
        std::snprintf(referenceText, sizeof(referenceText), "{\"variablesReference\":%lld}", static_cast<long long>(locals.Value));
        response = adapter.Request("variables", referenceText);
        // natvis 로 값이 보인다: `Float` 은 수, `Vector2` 는 괄호, `String` 은 글자, `Array` 는 크기.
        Check(VariableNamed(response, "speed").find("\"value\":\"2.5") != std::string::npos, "a Float shows as its number");
        Check(VariableNamed(response, "position").find("\"value\":\"(1") != std::string::npos, "a Vector2 shows as its components");
        Check(VariableNamed(response, "name").find("hello") != std::string::npos, "a String shows its text");
        const std::string numbers = VariableNamed(response, "numbers");
        Check(numbers.find("size=3") != std::string::npos && NumberAfter(numbers, "variablesReference") > 0, "an Array shows its size and opens");
        std::snprintf(referenceText, sizeof(referenceText), "{\"variablesReference\":%lld}",
            static_cast<long long>(NumberAfter(numbers, "variablesReference").Value));
        response = adapter.Request("variables", referenceText);
        Check(VariableNamed(response, "[0]").find("\"value\":\"1\"") != std::string::npos
                && VariableNamed(response, "[2]").find("\"value\":\"3\"") != std::string::npos,
            "the Array opens into its elements");
        std::string evaluate = "{\"expression\":\"speed\",\"frameId\":" + std::to_string(frame.Value) + "}";
        response = adapter.Request("evaluate", evaluate);
        Check(response.find("\"result\":\"2.5") != std::string::npos, "an expression in the frame evaluates");

        adapter.Request("next", threadText);
        stopped = adapter.WaitEvent("stopped");
        Check(stopped.find("\"reason\":\"step\"") != std::string::npos, "a step stops as a step");
        response = adapter.Request("stackTrace", threadText);
        Check(NumberAfter(response, "line") == Int64(breakLine.Value + 1), "on the next source line");

        // **다시 실은 복사본에서도 멈춘다.** 스크립트를 부르지 않게 한 채로 중단점을 남겨 두고 다음 세대를 싣는다 - 어댑터가 새 모듈에 다시 건다.
        std::error_code ignored;
        fs::remove(work / "run", ignored);
        adapter.Request("continue", threadText);
        Sleep(100);
        Touch(work / "reload");
        Check(WaitForGeneration(work, "2"), "the target loads the second copy");
        Touch(work / "run");
        stopped = adapter.WaitEvent("stopped");
        Check(stopped.find("\"reason\":\"breakpoint\"") != std::string::npos, "the breakpoint stops in the reloaded copy");
        response = adapter.Request("stackTrace", threadText);
        const std::string current = "TargetScript_gen" + ReadAll(work / "generation") + "!";
        Check(current != "TargetScript_gen1!" && response.find(current) != std::string::npos, "and the frame is in the copy loaded last");

        // 일시정지.
        adapter.Request("setBreakpoints", clearBreakpoints);
        adapter.Request("continue", threadText);
        Sleep(200);
        adapter.Request("pause", threadText);
        stopped = adapter.WaitEvent("stopped");
        Check(stopped.find("\"reason\":\"pause\"") != std::string::npos, "pause stops the running target");
        adapter.Request("continue", NumberAfter(stopped, "threadId") > 0 ? threadText : std::string("{}"));

        // 떼면 대상은 그대로 돈다. 중단점이 남아 있었다면 스크립트를 부르다 죽는다.
        response = adapter.Request("disconnect", "{}");
        Check(response.find("\"success\":true") != std::string::npos, "disconnect answers");
        DWORD exitCode = 1;
        Check(adapter.WaitExit(10000, exitCode) && exitCode == 0, "the adapter ends after disconnect");
        Sleep(300);
        Check(target.IsRunning(), "the target keeps running after the adapter leaves");

        // **어댑터가 죽어도 대상은 산다**(`DEBUG_PROCESS_DETACH_ON_EXIT`). 기본은 디버거가 끝날 때 대상도 끝낸다.
        Process second;
        Check(second.Start(adapterExe, L"", true), "a second adapter starts");
        second.Request("initialize", "{\"adapterID\":\"jbro\"}");
        response = second.Request("attach", attachText, 60000);
        Check(response.find("\"success\":true") != std::string::npos, "and attaches again");
        second.Request("configurationDone");
        Sleep(200);
        second.Kill();
        Sleep(500);
        Check(target.IsRunning(), "killing an attached adapter leaves the target running");

        Touch(work / "quit");
        exitCode = 1;
        Check(target.WaitExit(10000, exitCode) && exitCode == 0, "the target ends cleanly when asked");
    }
}

JBro::Int32 RunDebugAdapterTests()
{
    try
    {
        TestTheAdapterDebugsAScriptAcrossAReload();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "Debug adapter tests passed." << std::endl;
    return 0;
}
