#include <JBro/Platform/WindowsPlatform.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <AclAPI.h>
#include <sddl.h>

#include <cstdint>
#include <string>

// 에디터와 디버그 어댑터 사이의 로컬 파이프다(D-330 3 단계). 서버(에디터)가 이름 있는 파이프를 열고, 클라이언트(어댑터)가 그 이름으로 붙는다.
// 겹치기(overlapped)로 열어 메인 스레드가 프레임마다 불러도 막히지 않는다 - 붙기·읽기는 비동기로 걸어 두고 끝났을 때만 거둔다.
// 보안 서술자로 지금 사용자만 열 수 있게 한다. 다른 사용자·원격은 닿지 않는다.

namespace JBro
{
    namespace
    {
        constexpr DWORD BufferBytes = 64 * 1024;

        std::wstring ToWide(const char* utf8)
        {
            if (utf8 == nullptr || utf8[0] == '\0')
            {
                return std::wstring();
            }
            const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
            if (length <= 1)
            {
                return std::wstring();
            }
            std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
            return wide;
        }

        class WindowsLocalPipe final : public ILocalPipe
        {
        public:
            explicit WindowsLocalPipe(HANDLE pipe)
                : m_pipe(pipe)
            {
                m_connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                m_readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                ArmConnect();
            }

            ~WindowsLocalPipe() override
            {
                if (m_pipe != INVALID_HANDLE_VALUE)
                {
                    CancelIoEx(m_pipe, nullptr);
                    if (m_connected)
                    {
                        DisconnectNamedPipe(m_pipe);
                    }
                    CloseHandle(m_pipe);
                }
                if (m_connectEvent != nullptr)
                {
                    CloseHandle(m_connectEvent);
                }
                if (m_readEvent != nullptr)
                {
                    CloseHandle(m_readEvent);
                }
            }

            WindowsLocalPipe(const WindowsLocalPipe&) = delete;
            WindowsLocalPipe& operator=(const WindowsLocalPipe&) = delete;

            Bool IsConnected() override
            {
                Pump();
                return m_connected;
            }

            std::size_t Receive(String& into) override
            {
                Pump();
                const std::size_t before = into.size();
                while (m_connected && m_readPending)
                {
                    DWORD read = 0;
                    if (GetOverlappedResult(m_pipe, &m_readOverlapped, &read, FALSE))
                    {
                        m_readPending = false;
                        ResetEvent(m_readEvent);
                        into.append(m_readBuffer, read);
                        ArmRead();
                        continue;
                    }
                    const DWORD error = GetLastError();
                    if (error == ERROR_IO_INCOMPLETE)
                    {
                        break;
                    }
                    Drop();
                }
                return into.size() - before;
            }

            Bool Send(const char* data, std::size_t bytes) override
            {
                if (false == IsConnected())
                {
                    return false;
                }
                std::size_t written = 0;
                while (written < bytes)
                {
                    OVERLAPPED overlapped = {};
                    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                    const DWORD chunk = static_cast<DWORD>((std::min)(bytes - written, static_cast<std::size_t>(BufferBytes)));
                    DWORD sent = 0;
                    const BOOL ok = WriteFile(m_pipe, data + written, chunk, nullptr, &overlapped);
                    if (FALSE == ok && GetLastError() != ERROR_IO_PENDING)
                    {
                        CloseHandle(overlapped.hEvent);
                        Drop();
                        return false;
                    }
                    const BOOL done = GetOverlappedResult(m_pipe, &overlapped, &sent, TRUE);
                    CloseHandle(overlapped.hEvent);
                    if (FALSE == done || sent == 0)
                    {
                        Drop();
                        return false;
                    }
                    written += sent;
                }
                return true;
            }

        private:
            void ArmConnect()
            {
                m_connectOverlapped = {};
                m_connectOverlapped.hEvent = m_connectEvent;
                ResetEvent(m_connectEvent);
                const BOOL ok = ConnectNamedPipe(m_pipe, &m_connectOverlapped);
                const DWORD error = GetLastError();
                if (ok != FALSE || error == ERROR_PIPE_CONNECTED)
                {
                    m_connected = true;
                    m_connectPending = false;
                    ArmRead();
                }
                else if (error == ERROR_IO_PENDING)
                {
                    m_connectPending = true;
                }
                else
                {
                    m_connectPending = false;
                }
            }

            void ArmRead()
            {
                if (false == m_connected || m_readPending)
                {
                    return;
                }
                m_readOverlapped = {};
                m_readOverlapped.hEvent = m_readEvent;
                ResetEvent(m_readEvent);
                const BOOL ok = ReadFile(m_pipe, m_readBuffer, BufferBytes, nullptr, &m_readOverlapped);
                if (ok != FALSE)
                {
                    // 바로 읽혔다. `GetOverlappedResult` 가 이벤트로 거두도록 그대로 둔다.
                    SetEvent(m_readEvent);
                    m_readPending = true;
                }
                else if (GetLastError() == ERROR_IO_PENDING)
                {
                    m_readPending = true;
                }
                else
                {
                    Drop();
                }
            }

            void Pump()
            {
                if (m_connectPending)
                {
                    DWORD ignored = 0;
                    if (GetOverlappedResult(m_pipe, &m_connectOverlapped, &ignored, FALSE))
                    {
                        m_connectPending = false;
                        m_connected = true;
                        ArmRead();
                    }
                    else if (GetLastError() != ERROR_IO_INCOMPLETE)
                    {
                        m_connectPending = false;
                    }
                }
            }

            // 클라이언트가 끊겼다. 다음 클라이언트를 기다린다.
            void Drop()
            {
                CancelIoEx(m_pipe, nullptr);
                if (m_connected)
                {
                    DisconnectNamedPipe(m_pipe);
                }
                m_connected = false;
                m_readPending = false;
                ArmConnect();
            }

            HANDLE m_pipe = INVALID_HANDLE_VALUE;
            HANDLE m_connectEvent = nullptr;
            HANDLE m_readEvent = nullptr;
            OVERLAPPED m_connectOverlapped = {};
            OVERLAPPED m_readOverlapped = {};
            char m_readBuffer[BufferBytes] = {};
            Bool m_connected = false;
            Bool m_connectPending = false;
            Bool m_readPending = false;
        };

        // 지금 사용자만 닿게 하는 보안 서술자다. 소유자(현재 사용자)에게만 모든 권한을 준다 - 다른 사용자도 원격도 받지 않는다.
        Bool MakeOwnerOnlySecurity(SECURITY_ATTRIBUTES& attributes, PSECURITY_DESCRIPTOR& descriptor)
        {
            // `D:P` 는 상속을 끊은 DACL, `(A;;GA;;;OW)` 는 소유자에게 전체 권한. 소유자는 이 프로세스를 만든 사용자다.
            if (FALSE == ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr))
            {
                return false;
            }
            attributes = {};
            attributes.nLength = sizeof(attributes);
            attributes.lpSecurityDescriptor = descriptor;
            attributes.bInheritHandle = FALSE;
            return true;
        }
    }

    UInt32 WindowsPlatform::GetProcessId() const
    {
        return UInt32(static_cast<std::uint32_t>(GetCurrentProcessId()));
    }

    OwnerPtr<ILocalPipe> WindowsPlatform::OpenLocalPipe(const char* name)
    {
        if (name == nullptr || name[0] == '\0')
        {
            return nullptr;
        }
        const std::wstring path = L"\\\\.\\pipe\\" + ToWide(name);
        SECURITY_ATTRIBUTES attributes = {};
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (false == MakeOwnerOnlySecurity(attributes, descriptor))
        {
            return nullptr;
        }
        const HANDLE pipe = CreateNamedPipeW(path.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, BufferBytes, BufferBytes, 0, &attributes);
        LocalFree(descriptor);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            // 이미 있는 이름이거나 만들지 못했다.
            return nullptr;
        }
        return MakeOwnerPtr<WindowsLocalPipe>(pipe);
    }
}
