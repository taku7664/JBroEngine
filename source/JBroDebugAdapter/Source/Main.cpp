#include "DapChannel.h"
#include "DebugSession.h"

#include <Windows.h>
#include <DbgHelp.h>

#include <cwchar>

namespace
{
    // 어댑터가 죽으면 `%TEMP%\JBroDebugAdapter-<pid>.dmp` 를 남긴다. 붙어 있던 에디터는 `DebugSetProcessKillOnExit(FALSE)` 로 살아남는다.
    LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* exception)
    {
        wchar_t folder[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, folder);
        wchar_t path[MAX_PATH * 2] = {};
        std::swprintf(path, MAX_PATH * 2, L"%sJBroDebugAdapter-%lu.dmp", folder, GetCurrentProcessId());
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION information = {};
            information.ThreadId = GetCurrentThreadId();
            information.ExceptionPointers = exception;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithDataSegs, &information, nullptr, nullptr);
            CloseHandle(file);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

// JBro Script Editor 가 띄우는 디버그 어댑터다(D-319). 표준 입출력으로 Debug Adapter Protocol 을 말하고, 스크립트 DLL 을 디버깅한다.
// 엔진 모듈은 JBroCore 만 링크한다 - 나머지는 DbgEng 과 Windows API 다. DbgEng DLL 은 이 실행 파일 옆에 둔다(System32 의 것은 dbghelp 를 거절한다).
int wmain()
{
    using namespace JBro::DebugAdapter;
    SetUnhandledExceptionFilter(WriteCrashDump);
    DapChannel channel(GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE));
    DebugSession session(channel);
    return static_cast<int>(session.Run().Value);
}
