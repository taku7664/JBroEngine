#include <JBro/Platform/WindowsPlatform.h>

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

// 자식 프로세스다(cpp-script-plan §3.4). 에디터가 MSBuild 로 스크립트를 빌드하는 길이다.
// 기존 엔진(`CCompilePipeline`)은 `std::async` 스레드에서 `WaitForSingleObject` 로 끝까지 기다렸다. 여기서는 기다리지 않고
// 에디터가 프레임마다 묻는다 - 스레드가 없으니 끝내는 것도 핸들 하나로 된다.
namespace JBro
{
    namespace
    {
        std::wstring Widen(const char* utf8)
        {
            if (utf8 == nullptr || utf8[0] == '\0')
            {
                return std::wstring();
            }
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
            if (length <= 1)
            {
                return std::wstring();
            }
            std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), length);
            return wide;
        }
    }

    ChildProcess WindowsPlatform::StartProcess(const char* utf8CommandLine, const char* utf8WorkingFolder, const char* utf8OutputFile)
    {
        std::wstring command = Widen(utf8CommandLine);
        if (command.empty())
        {
            return {};
        }
        // 출력은 파일로 받는다. 파이프로 받으면 읽는 쪽이 비워 주지 않는 동안 자식이 멈춘다 - 프레임마다 읽으러 오는 에디터와 맞지 않다.
        SECURITY_ATTRIBUTES inherit = {};
        inherit.nLength = sizeof(inherit);
        inherit.bInheritHandle = TRUE;
        const std::wstring outputPath = utf8OutputFile != nullptr ? Widen(utf8OutputFile) : std::wstring(L"NUL");
        HANDLE output = CreateFileW(outputPath.empty() ? L"NUL" : outputPath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE)
        {
            return {};
        }
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

        // **띄운 것 전부를 한 묶음(Job)으로 든다.** 닫으면 묶음째 끝난다 - MSBuild 가 띄운 컴파일러까지.
        HANDLE group = CreateJobObjectW(nullptr, nullptr);
        if (group != nullptr)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(group, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = input != INVALID_HANDLE_VALUE ? input : nullptr;
        PROCESS_INFORMATION info = {};
        const std::wstring folder = Widen(utf8WorkingFolder);
        // 묶음에 넣기 전에 돌지 않게 멈춘 채로 띄운다. 그 사이에 띄운 것이 묶음 밖으로 새지 않는다.
        const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, folder.empty() ? nullptr : folder.c_str(), &startup, &info);
        CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
        {
            CloseHandle(input);
        }
        if (FALSE == created)
        {
            if (group != nullptr)
            {
                CloseHandle(group);
            }
            return {};
        }
        if (group != nullptr && FALSE == AssignProcessToJobObject(group, info.hProcess))
        {
            CloseHandle(group);
            group = nullptr;
        }
        ResumeThread(info.hThread);
        CloseHandle(info.hThread);

        ChildProcess process;
        process.process = info.hProcess;
        process.group = group;
        return process;
    }

    ProcessStatus WindowsPlatform::PollProcess(const ChildProcess& process, std::int32_t& exitCode)
    {
        if (process.process == nullptr)
        {
            return ProcessStatus::Invalid;
        }
        if (WaitForSingleObject(static_cast<HANDLE>(process.process), 0) != WAIT_OBJECT_0)
        {
            return ProcessStatus::Running;
        }
        DWORD code = 0;
        if (FALSE == GetExitCodeProcess(static_cast<HANDLE>(process.process), &code))
        {
            return ProcessStatus::Invalid;
        }
        exitCode = static_cast<std::int32_t>(code);
        return ProcessStatus::Exited;
    }

    void WindowsPlatform::CloseProcess(ChildProcess& process)
    {
        const bool running = process.process != nullptr
            && WaitForSingleObject(static_cast<HANDLE>(process.process), 0) != WAIT_OBJECT_0;
        if (process.group != nullptr)
        {
            // `KILL_ON_JOB_CLOSE` 라 이것을 닫으면 묶음 안에서 아직 도는 것이 모두 끝난다. **스스로 끝난 뒤에는 그 설정을 풀고 닫는다** -
            // 컴파일러가 띄운 PDB 서버(`mspdbsrv`)처럼 다른 빌드가 함께 쓰는 것이 묶음에 남아 있을 수 있다.
            if (false == running)
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
                SetInformationJobObject(static_cast<HANDLE>(process.group), JobObjectExtendedLimitInformation, &limits, sizeof(limits));
            }
            CloseHandle(static_cast<HANDLE>(process.group));
        }
        else if (process.process != nullptr && WaitForSingleObject(static_cast<HANDLE>(process.process), 0) != WAIT_OBJECT_0)
        {
            TerminateProcess(static_cast<HANDLE>(process.process), 1);
        }
        if (process.process != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(process.process));
        }
        process = {};
    }

    bool WindowsPlatform::LaunchProcess(const char* utf8CommandLine, const char* utf8WorkingFolder)
    {
        std::wstring command = Widen(utf8CommandLine);
        if (command.empty())
        {
            return false;
        }
        const std::wstring folder = Widen(utf8WorkingFolder);
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info = {};
        if (FALSE == CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                folder.empty() ? nullptr : folder.c_str(), &startup, &info))
        {
            return false;
        }
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return true;
    }

    String WindowsPlatform::ReadEnvironmentVariable(const char* name) const
    {
        const std::wstring wideName = Widen(name);
        if (wideName.empty())
        {
            return String();
        }
        const DWORD length = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
        if (length == 0)
        {
            return String();
        }
        std::wstring value(length, L'\0');
        const DWORD written = GetEnvironmentVariableW(wideName.c_str(), value.data(), length);
        value.resize(written);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        String utf8(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
        if (bytes > 0)
        {
            WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), bytes, nullptr, nullptr);
        }
        return utf8;
    }
}
