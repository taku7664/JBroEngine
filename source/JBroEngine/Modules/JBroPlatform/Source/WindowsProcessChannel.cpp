#include <JBro/Platform/WindowsPlatform.h>
#include <Windows.h>
#include <imm.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <system_error>
#include <vector>
#pragma comment(lib, "imm32.lib")

namespace JBro
{
    namespace
    {
        struct NativeHandle
        {
            HANDLE value = nullptr;
            ~NativeHandle()
            {
                if (value != nullptr && value != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(value);
                }
            }
        };
        std::wstring Wide(const char* text)
        {
            if (text == nullptr || *text == 0)
            {
                return {};
            }
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
            if (size <= 0)
            {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), size);
            result.pop_back();
            return result;
        }

        class WindowsProcessChannel final : public IProcessChannel
        {
        public:
            ~WindowsProcessChannel() override
            {
                m_stopping = true;
                if (m_job.value != nullptr)
                {
                    TerminateJobObject(m_job.value, 1);
                }
                if (m_process.value != nullptr)
                {
                    TerminateProcess(m_process.value, 1);
                }
                m_wake.notify_all();
                if (m_writer.joinable())
                {
                    m_writer.join();
                }
                if (m_reader.joinable())
                {
                    m_reader.join();
                }
                if (m_errorReader.joinable())
                {
                    m_errorReader.join();
                }
            }
            Bool Start(const char* command, const char* folder)
            {
                std::wstring wide = Wide(command);
                const std::wstring working = Wide(folder);
                if (wide.empty())
                {
                    return false;
                }
                SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
                NativeHandle childInput, childOutput, childError;
                if (!CreatePipe(&childInput.value, &m_input.value, &inherit, 0)
                    || !CreatePipe(&m_output.value, &childOutput.value, &inherit, 0)
                    || !CreatePipe(&m_error.value, &childError.value, &inherit, 0))
                {
                    return false;
                }
                if (!SetHandleInformation(m_input.value, HANDLE_FLAG_INHERIT, 0)
                    || !SetHandleInformation(m_output.value, HANDLE_FLAG_INHERIT, 0)
                    || !SetHandleInformation(m_error.value, HANDLE_FLAG_INHERIT, 0))
                {
                    return false;
                }
                m_job.value = CreateJobObjectW(nullptr, nullptr);
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (m_job.value == nullptr || !SetInformationJobObject(m_job.value,
                    JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                {
                    return false;
                }
                SIZE_T attributeSize = 0;
                InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
                std::vector<std::byte> storage(attributeSize);
                auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
                if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize))
                {
                    return false;
                }
                HANDLE inherited[]{childInput.value, childOutput.value, childError.value};
                const BOOL updated = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    inherited, sizeof(inherited), nullptr, nullptr);
                STARTUPINFOEXW startup{};
                startup.StartupInfo.cb = sizeof(startup);
                startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                startup.StartupInfo.hStdInput = childInput.value;
                startup.StartupInfo.hStdOutput = childOutput.value;
                startup.StartupInfo.hStdError = childError.value;
                startup.lpAttributeList = attributes;
                PROCESS_INFORMATION process{};
                const BOOL created = updated && CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE,
                    CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                    working.empty() ? nullptr : working.c_str(), &startup.StartupInfo, &process);
                DeleteProcThreadAttributeList(attributes);
                if (!created)
                {
                    return false;
                }
                m_process.value = process.hProcess;
                NativeHandle primaryThread;
                primaryThread.value = process.hThread;
                if (!AssignProcessToJobObject(m_job.value, m_process.value))
                {
                    return false;
                }
                m_reader = std::thread([this] { Drain(m_output.value, m_received); });
                m_errorReader = std::thread([this] { Drain(m_error.value, m_errors); });
                m_writer = std::thread([this] { Flush(); });
                return ResumeThread(primaryThread.value) != static_cast<DWORD>(-1);
            }
            Bool Write(std::string_view bytes) override
            {
                std::lock_guard lock(m_mutex);
                if (m_stopping || m_failed || bytes.size() > Limit - m_pendingBytes)
                {
                    return false;
                }
                m_pending.emplace_back(bytes);
                m_pendingBytes += bytes.size();
                m_wake.notify_one();
                return true;
            }
            Bool Read(String& bytes, Bool standardError) override
            {
                std::lock_guard lock(m_mutex);
                String& source = standardError ? m_errors : m_received;
                bytes.swap(source);
                source.clear();
                return !bytes.empty();
            }
            Bool IsRunning() const override
            {
                DWORD exit = 0;
                return !m_failed && m_process.value != nullptr
                    && GetExitCodeProcess(m_process.value, &exit) && exit == STILL_ACTIVE;
            }
        private:
            void Drain(HANDLE pipe, String& target)
            {
                char buffer[8192];
                DWORD size = 0;
                while (!m_stopping && ReadFile(pipe, buffer, sizeof(buffer), &size, nullptr) && size != 0)
                {
                    std::lock_guard lock(m_mutex);
                    if (size > Limit - target.size())
                    {
                        m_failed = true;
                        return;
                    }
                    target.append(buffer, size);
                }
            }
            void Flush()
            {
                while (!m_stopping)
                {
                    String bytes;
                    {
                        std::unique_lock lock(m_mutex);
                        m_wake.wait(lock, [this] { return m_stopping || !m_pending.empty(); });
                        if (m_stopping)
                        {
                            return;
                        }
                        bytes = std::move(m_pending.front());
                        m_pending.pop_front();
                        m_pendingBytes -= bytes.size();
                    }
                    std::size_t offset = 0;
                    while (offset < bytes.size() && !m_stopping)
                    {
                        DWORD written = 0;
                        const DWORD count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 65536));
                        if (!WriteFile(m_input.value, bytes.data() + offset, count, &written, nullptr) || written == 0)
                        {
                            m_failed = true;
                            return;
                        }
                        offset += written;
                    }
                }
            }
            static constexpr std::size_t Limit = 16 * 1024 * 1024;
            NativeHandle m_process, m_job, m_input, m_output, m_error;
            std::atomic_bool m_stopping = false, m_failed = false;
            std::mutex m_mutex;
            std::condition_variable m_wake;
            std::deque<String> m_pending;
            std::size_t m_pendingBytes = 0;
            String m_received, m_errors;
            std::thread m_writer, m_reader, m_errorReader;
        };
    }
    OwnerPtr<IProcessChannel> WindowsPlatform::StartProcessChannel(const char* command, const char* workingFolder)
    {
        auto channel = MakeOwnerPtr<WindowsProcessChannel>();
        try
        {
            if (!channel->Start(command, workingFolder))
            {
                return nullptr;
            }
        }
        catch (const std::system_error&)
        {
            return nullptr;
        }
        return channel;
    }
    void WindowsPlatform::SetTextInputPosition(WindowHandle window, Float x, Float y, Float lineHeight)
    {
        HWND hwnd = reinterpret_cast<HWND>(window.value);
        HIMC context = hwnd != nullptr ? ImmGetContext(hwnd) : nullptr;
        if (context == nullptr)
        {
            return;
        }
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_FORCE_POSITION;
        composition.ptCurrentPos = {static_cast<LONG>(x.Get()), static_cast<LONG>(y.Get())};
        ImmSetCompositionWindow(context, &composition);
        CANDIDATEFORM candidate{};
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = composition.ptCurrentPos;
        candidate.rcArea = {composition.ptCurrentPos.x, composition.ptCurrentPos.y,
            composition.ptCurrentPos.x + 1, composition.ptCurrentPos.y + static_cast<LONG>(lineHeight.Get())};
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(hwnd, context);
    }
}
