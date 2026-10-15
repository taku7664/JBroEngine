#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

// 디버그 어댑터 시험이 붙는 대상이다(D-330 3 단계). **에디터를 흉내 낸다** - 어댑터는 세우지 않고 붙어 메모리만 읽고,
// 중단점과 멈춤은 이 프로그램이 파이프로 주고받는다(에디터 안에서는 엔진이 그 일을 한다).
//
// 에디터처럼 스크립트 DLL 을 세대마다 다른 이름의 복사본으로 싣고, 통로 `\\.\pipe\jbro-debug-<pid>` 를 연다.
// 어댑터가 `setbp <주소>` 를 보내면 그 자리를 무장하고 `bp <주소> 1` 로 답한다. 스크립트가 그 줄(BREAK_HERE)에 이르면
// 자기 문맥을 떠 `stopped <주소> <문맥주소> <스레드>` 를 보내고, `continue` 가 올 때까지 통로를 돌며 기다린다.
//
// 시험과는 작업 폴더의 파일로도 말한다:
//   run     있으면 매 틱마다 스크립트를 부른다.  reload  다음 세대 복사본을 싣는다.  quit  끝낸다.
namespace
{
    using TickFunction = int (*)(int);
    using SetBreakFunction = void (*)(void (*)());

    std::wstring g_folder;
    HANDLE g_pipe = INVALID_HANDLE_VALUE;

    bool Exists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // ── 파이프(에디터 쪽) ──
    std::string g_inbox;
    unsigned long long g_armedAddress = 0;
    bool g_armed = false;
    bool g_continue = false;
    bool g_stop = false;
    bool g_clientConnected = false;

    void PipeSend(const std::string& line)
    {
        if (g_pipe == INVALID_HANDLE_VALUE || false == g_clientConnected)
        {
            return;
        }
        const std::string packet = line + "\n";
        DWORD written = 0;
        if (FALSE == WriteFile(g_pipe, packet.data(), static_cast<DWORD>(packet.size()), &written, nullptr))
        {
            g_clientConnected = false;
        }
    }

    void HandleLine(const std::string& line)
    {
        if (line.rfind("setbp ", 0) == 0)
        {
            g_armedAddress = std::strtoull(line.c_str() + 6, nullptr, 16);
            g_armed = true;
            char reply[48] = {};
            std::snprintf(reply, sizeof(reply), "bp %llx 1", g_armedAddress);
            PipeSend(reply);
        }
        else if (line.rfind("clearbp", 0) == 0)
        {
            g_armed = false;
        }
        else if (line.rfind("continue", 0) == 0 || line.rfind("step", 0) == 0)
        {
            // 대상은 엔진이 아니라 실제로 한 줄 갈 수 없다 - 멈춤에서 풀기만 한다(한 줄 가기 자체는 엔진 쪽 시험이 본다).
            g_continue = true;
        }
        else if (line.rfind("stop", 0) == 0)
        {
            g_stop = true;
            g_continue = true;
        }
    }

    // 와 있는 명령을 처리한다. 붙지 않았으면 붙기를 한 번 시도한다.
    void PipePump()
    {
        if (g_pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }
        if (false == g_clientConnected)
        {
            if (ConnectNamedPipe(g_pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
            {
                g_clientConnected = true;
            }
            else
            {
                return;
            }
        }
        DWORD available = 0;
        if (FALSE == PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &available, nullptr))
        {
            g_clientConnected = false;
            return;
        }
        if (available > 0)
        {
            std::string chunk(available, '\0');
            DWORD read = 0;
            ReadFile(g_pipe, chunk.data(), available, &read, nullptr);
            g_inbox.append(chunk.data(), read);
        }
        std::size_t newline = g_inbox.find('\n');
        while (newline != std::string::npos)
        {
            std::string line = g_inbox.substr(0, newline);
            if (false == line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            g_inbox.erase(0, newline + 1);
            if (false == line.empty())
            {
                HandleLine(line);
            }
            newline = g_inbox.find('\n');
        }
    }

    // 스크립트가 BREAK_HERE 에서 부른다. 문맥을 떠 멈춤을 알리고 `continue` 를 기다린다.
    void OnScriptBreak()
    {
        if (false == g_armed || false == g_clientConnected)
        {
            return;
        }
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_FULL;
        RtlCaptureContext(&context);
        char line[96] = {};
        std::snprintf(line, sizeof(line), "stopped %llx %llx %lu", g_armedAddress,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&context)), static_cast<unsigned long>(GetCurrentThreadId()));
        g_continue = false;
        PipeSend(line);
        const ULONGLONG deadline = GetTickCount64() + 30000;
        while (false == g_continue && GetTickCount64() < deadline && g_clientConnected)
        {
            PipePump();
            Sleep(2);
        }
        PipeSend("resumed");
    }

    HMODULE Load(const std::wstring& source, int generation, TickFunction& tick)
    {
        const std::wstring copy = g_folder + L"\\TargetScript.gen" + std::to_wstring(generation) + L".dll";
        CopyFileW(source.c_str(), copy.c_str(), FALSE);
        HMODULE module = LoadLibraryW(copy.c_str());
        tick = module != nullptr ? reinterpret_cast<TickFunction>(GetProcAddress(module, "TargetTick")) : nullptr;
        if (module != nullptr)
        {
            if (auto setBreak = reinterpret_cast<SetBreakFunction>(GetProcAddress(module, "TargetSetBreakCallback")))
            {
                setBreak(&OnScriptBreak);
            }
        }
        const std::wstring marker = g_folder + L"\\generation";
        HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            char text[16] = {};
            const int length = std::snprintf(text, sizeof(text), "%d", tick != nullptr ? generation : 0);
            DWORD written = 0;
            WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
            CloseHandle(file);
        }
        return module;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        return 2;
    }
    g_folder = argv[1];
    wchar_t self[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    std::wstring source = self;
    source = source.substr(0, source.find_last_of(L"\\/") + 1) + L"JBroDebugAdapterTargetScript.dll";

    // 에디터처럼 통로를 연다. 어댑터가 붙기 전에 열려 있어야 한다.
    wchar_t pipeName[64] = {};
    std::swprintf(pipeName, 64, L"\\\\.\\pipe\\jbro-debug-%lu", GetCurrentProcessId());
    g_pipe = CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
        1, 64 * 1024, 64 * 1024, 0, nullptr);

    int generation = 1;
    TickFunction tick = nullptr;
    HMODULE module = Load(source, generation, tick);
    if (tick == nullptr)
    {
        return 3;
    }
    int frame = 0;
    volatile int sink = 0;
    const ULONGLONG deadline = GetTickCount64() + 120000;
    while (GetTickCount64() < deadline && false == Exists(g_folder + L"\\quit") && false == g_stop)
    {
        PipePump();
        if (Exists(g_folder + L"\\reload"))
        {
            for (int attempt = 0; attempt < 200 && FALSE == DeleteFileW((g_folder + L"\\reload").c_str()); ++attempt)
            {
                Sleep(10);
            }
            FreeLibrary(module);
            generation = generation + 1;
            module = Load(source, generation, tick);
            if (tick == nullptr)
            {
                return 4;
            }
        }
        if (Exists(g_folder + L"\\run"))
        {
            sink = tick(frame);
            frame = frame + 1;
        }
        Sleep(10);
    }
    (void)sink;
    if (g_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_pipe);
    }
    FreeLibrary(module);
    return 0;
}
