#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>

#include <limits>

namespace JBro
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"JBroEngineWindow";
        constexpr std::size_t ShadowLibrarySuffixCapacity = 64;
        volatile LONG64 ShadowLibrarySequence = 0;

        struct WindowsDynamicLibrary final
        {
            HMODULE Module = nullptr;
            wchar_t* ShadowPath = nullptr;
        };

        LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_CLOSE)
            {
                // This slot belongs to our window class and only stores its close flag.
                SetWindowLongPtrW(window, GWLP_USERDATA, 1);
                return 0;
            }

            return DefWindowProcW(window, message, wParam, lParam);
        }

        wchar_t* ConvertWindowTitle(const JStringView& title)
        {
            if (title.data == nullptr
                || title.size == 0
                || title.size > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
            {
                return nullptr;
            }

            const int sourceLength = static_cast<int>(title.size);
            const int wideLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                title.data,
                sourceLength,
                nullptr,
                0);
            if (wideLength == 0)
            {
                return nullptr;
            }

            HANDLE processHeap = GetProcessHeap();
            auto* wideTitle = static_cast<wchar_t*>(HeapAlloc(
                processHeap,
                0,
                (static_cast<SIZE_T>(wideLength) + 1) * sizeof(wchar_t)));
            if (wideTitle == nullptr)
            {
                return nullptr;
            }

            const int convertedLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                title.data,
                sourceLength,
                wideTitle,
                wideLength);
            if (convertedLength == 0)
            {
                HeapFree(processHeap, 0, wideTitle);
                return nullptr;
            }

            wideTitle[convertedLength] = L'\0';
            return wideTitle;
        }
    }

    bool WindowsPlatform::Initialize(const JMemoryContext&)
    {
        if (m_instance != nullptr)
        {
            return false;
        }

        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (instance == nullptr)
        {
            return false;
        }

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = WindowClassName;

        const ATOM atom = RegisterClassExW(&windowClass);
        if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        m_instance = instance;
        m_windowClassAtom = atom;
        m_ownsWindowClass = atom != 0;
        m_quitRequested = false;
        return true;
    }

    void WindowsPlatform::Shutdown()
    {
        if (m_ownsWindowClass && m_instance != nullptr)
        {
            UnregisterClassW(WindowClassName, static_cast<HINSTANCE>(m_instance));
        }

        m_ownsWindowClass = false;
        m_windowClassAtom = 0;
        m_instance = nullptr;
    }

    WindowHandle WindowsPlatform::OpenPlatformWindow(const WindowDesc& desc)
    {
        if (m_instance == nullptr
            || desc.width == 0
            || desc.height == 0
            || desc.width > static_cast<std::uint32_t>((std::numeric_limits<LONG>::max)())
            || desc.height > static_cast<std::uint32_t>((std::numeric_limits<LONG>::max)()))
        {
            return {};
        }

        constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;
        RECT windowRect = {
            0,
            0,
            static_cast<LONG>(desc.width),
            static_cast<LONG>(desc.height)};
        if (AdjustWindowRectEx(&windowRect, windowStyle, FALSE, 0) == FALSE)
        {
            return {};
        }

        wchar_t* convertedTitle = ConvertWindowTitle(desc.title);
        const wchar_t* windowTitle = convertedTitle != nullptr ? convertedTitle : L"JBro";

        HWND window = CreateWindowExW(
            0,
            WindowClassName,
            windowTitle,
            windowStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            static_cast<HINSTANCE>(m_instance),
            nullptr);

        if (convertedTitle != nullptr)
        {
            HeapFree(GetProcessHeap(), 0, convertedTitle);
        }

        if (window == nullptr)
        {
            return {};
        }

        if (desc.visible)
        {
            ShowWindow(window, SW_SHOW);
            UpdateWindow(window);
        }

        return {reinterpret_cast<std::uintptr_t>(window)};
    }

    void WindowsPlatform::ClosePlatformWindow(WindowHandle window)
    {
        HWND nativeWindow = reinterpret_cast<HWND>(window.value);
        if (nativeWindow != nullptr && IsWindow(nativeWindow))
        {
            DestroyWindow(nativeWindow);
        }
    }

    SurfaceHandle WindowsPlatform::CreateSurface(WindowHandle window)
    {
        HWND nativeWindow = reinterpret_cast<HWND>(window.value);
        if (nativeWindow == nullptr || false == IsWindow(nativeWindow))
        {
            return {};
        }

        return {window.value};
    }

    void WindowsPlatform::PumpEvents()
    {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                m_quitRequested = true;
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    void WindowsPlatform::WaitForEvents(std::uint32_t timeoutMilliseconds)
    {
        if (timeoutMilliseconds == 0)
        {
            return;
        }

        MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            timeoutMilliseconds,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
    }

    bool WindowsPlatform::ShouldClose(WindowHandle window) const
    {
        HWND nativeWindow = reinterpret_cast<HWND>(window.value);
        return m_quitRequested || nativeWindow == nullptr || false == IsWindow(nativeWindow)
            || GetWindowLongPtrW(nativeWindow, GWLP_USERDATA) != 0;
    }

    bool WindowsPlatform::GetWindowState(WindowHandle window, WindowState& state) const
    {
        state = {};
        const auto nativeWindow = reinterpret_cast<HWND>(window.value);
        RECT client = {};
        if (nativeWindow == nullptr || GetClientRect(nativeWindow, &client) == FALSE)
        {
            return false;
        }
        state.width = static_cast<std::uint32_t>(client.right - client.left);
        state.height = static_cast<std::uint32_t>(client.bottom - client.top);
        state.minimized = IsIconic(nativeWindow) != FALSE;
        return true;
    }

    DynamicLibrary WindowsPlatform::LoadDynamicLibrary(const char* utf8Path)
    {
        if (utf8Path == nullptr || utf8Path[0] == '\0')
        {
            return {};
        }

        const int wideLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8Path,
            -1,
            nullptr,
            0);
        if (wideLength == 0)
        {
            return {};
        }

        HANDLE processHeap = GetProcessHeap();
        auto* sourcePath = static_cast<wchar_t*>(HeapAlloc(
            processHeap,
            0,
            static_cast<SIZE_T>(wideLength) * sizeof(wchar_t)));
        if (sourcePath == nullptr)
        {
            return {};
        }

        const int convertedLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8Path,
            -1,
            sourcePath,
            wideLength);
        if (convertedLength == 0)
        {
            HeapFree(processHeap, 0, sourcePath);
            return {};
        }

        const std::size_t sourceLength = static_cast<std::size_t>(wideLength - 1);
        const std::size_t shadowCapacity =
            static_cast<std::size_t>(wideLength) + ShadowLibrarySuffixCapacity;
        auto* shadowPath = static_cast<wchar_t*>(HeapAlloc(
            processHeap,
            0,
            static_cast<SIZE_T>(shadowCapacity) * sizeof(wchar_t)));
        if (shadowPath == nullptr)
        {
            HeapFree(processHeap, 0, sourcePath);
            return {};
        }

        bool copied = false;
        for (std::uint32_t attempt = 0; attempt < 16; ++attempt)
        {
            if (wcscpy_s(shadowPath, shadowCapacity, sourcePath) != 0)
            {
                break;
            }
            const unsigned long long sequence = static_cast<unsigned long long>(
                InterlockedIncrement64(&ShadowLibrarySequence));
            const int suffixLength = swprintf_s(
                shadowPath + sourceLength,
                ShadowLibrarySuffixCapacity,
                L".jbro.%lu.%llu.dll",
                GetCurrentProcessId(),
                sequence);
            if (suffixLength <= 0)
            {
                break;
            }
            if (CopyFileW(sourcePath, shadowPath, TRUE) != FALSE)
            {
                copied = true;
                break;
            }
            const DWORD copyError = GetLastError();
            if (copyError != ERROR_FILE_EXISTS && copyError != ERROR_ALREADY_EXISTS)
            {
                break;
            }
        }
        HeapFree(processHeap, 0, sourcePath);
        if (false == copied)
        {
            HeapFree(processHeap, 0, shadowPath);
            return {};
        }

        const HMODULE module = LoadLibraryW(shadowPath);
        if (module == nullptr)
        {
            DeleteFileW(shadowPath);
            HeapFree(processHeap, 0, shadowPath);
            return {};
        }

        auto* library = static_cast<WindowsDynamicLibrary*>(HeapAlloc(
            processHeap,
            HEAP_ZERO_MEMORY,
            sizeof(WindowsDynamicLibrary)));
        if (library == nullptr)
        {
            FreeLibrary(module);
            DeleteFileW(shadowPath);
            HeapFree(processHeap, 0, shadowPath);
            return {};
        }
        library->Module = module;
        library->ShadowPath = shadowPath;
        return {library};
    }

    void* WindowsPlatform::GetSymbol(DynamicLibrary library, const char* name)
    {
        if (library.opaque == nullptr || name == nullptr || name[0] == '\0')
        {
            return nullptr;
        }

        const auto* nativeLibrary = static_cast<const WindowsDynamicLibrary*>(
            library.opaque);
        if (nativeLibrary->Module == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<void*>(GetProcAddress(nativeLibrary->Module, name));
    }

    void WindowsPlatform::UnloadDynamicLibrary(DynamicLibrary library)
    {
        if (library.opaque == nullptr)
        {
            return;
        }

        auto* nativeLibrary = static_cast<WindowsDynamicLibrary*>(library.opaque);
        if (nativeLibrary->Module != nullptr)
        {
            FreeLibrary(nativeLibrary->Module);
        }
        if (nativeLibrary->ShadowPath != nullptr)
        {
            DeleteFileW(nativeLibrary->ShadowPath);
            HeapFree(GetProcessHeap(), 0, nativeLibrary->ShadowPath);
        }
        HeapFree(GetProcessHeap(), 0, nativeLibrary);
    }
}
