#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>
#include <windowsx.h>

#include <limits>

namespace JBro
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"JBroEngineWindow";
        // 창 클래스의 여분 바이트에 플랫폼 포인터를 둔다. GWLP_USERDATA 는 이미
        // 닫기 플래그가 쓰고 있다.
        constexpr int PlatformSlot = 0;
        constexpr std::size_t ShadowLibrarySuffixCapacity = 64;
        volatile LONG64 ShadowLibrarySequence = 0;

        struct WindowsDynamicLibrary final
        {
            HMODULE Module = nullptr;
            wchar_t* ShadowPath = nullptr;
        };


        // 가상 키 코드를 물리 키로 옮긴다. **좌우가 갈리는 키는 스캔코드로 가른다** -
        // VK_SHIFT 는 어느 쪽인지 알려주지 않고, 에디터 단축키는 그것을 구분해야 한다.
        Key TranslateKey(WPARAM wParam, LPARAM lParam)
        {
            const UINT scanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
            const bool extended = (lParam & (1 << 24)) != 0;

            switch (wParam)
            {
            case VK_TAB: return Key::Tab;
            case VK_LEFT: return Key::Left;
            case VK_RIGHT: return Key::Right;
            case VK_UP: return Key::Up;
            case VK_DOWN: return Key::Down;
            case VK_PRIOR: return Key::PageUp;
            case VK_NEXT: return Key::PageDown;
            case VK_HOME: return Key::Home;
            case VK_END: return Key::End;
            case VK_INSERT: return Key::Insert;
            case VK_DELETE: return Key::Delete;
            case VK_BACK: return Key::Backspace;
            case VK_SPACE: return Key::Space;
            case VK_RETURN: return extended ? Key::KeypadEnter : Key::Enter;
            case VK_ESCAPE: return Key::Escape;
            case VK_APPS: return Key::Menu;
            case VK_CAPITAL: return Key::CapsLock;
            case VK_SCROLL: return Key::ScrollLock;
            case VK_NUMLOCK: return Key::NumLock;
            case VK_SNAPSHOT: return Key::PrintScreen;
            case VK_PAUSE: return Key::Pause;
            case VK_OEM_7: return Key::Apostrophe;
            case VK_OEM_COMMA: return Key::Comma;
            case VK_OEM_MINUS: return Key::Minus;
            case VK_OEM_PERIOD: return Key::Period;
            case VK_OEM_2: return Key::Slash;
            case VK_OEM_1: return Key::Semicolon;
            case VK_OEM_PLUS: return Key::Equal;
            case VK_OEM_4: return Key::LeftBracket;
            case VK_OEM_5: return Key::Backslash;
            case VK_OEM_6: return Key::RightBracket;
            case VK_OEM_3: return Key::GraveAccent;
            case VK_DECIMAL: return Key::KeypadDecimal;
            case VK_DIVIDE: return Key::KeypadDivide;
            case VK_MULTIPLY: return Key::KeypadMultiply;
            case VK_SUBTRACT: return Key::KeypadSubtract;
            case VK_ADD: return Key::KeypadAdd;

            case VK_SHIFT:
            {
                // 스캔코드를 좌우가 갈린 가상 키로 되돌린다.
                const UINT resolved = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
                return resolved == VK_RSHIFT ? Key::RightShift : Key::LeftShift;
            }
            case VK_CONTROL:
                return extended ? Key::RightControl : Key::LeftControl;
            case VK_MENU:
                return extended ? Key::RightAlt : Key::LeftAlt;
            case VK_LSHIFT: return Key::LeftShift;
            case VK_RSHIFT: return Key::RightShift;
            case VK_LCONTROL: return Key::LeftControl;
            case VK_RCONTROL: return Key::RightControl;
            case VK_LMENU: return Key::LeftAlt;
            case VK_RMENU: return Key::RightAlt;
            case VK_LWIN: return Key::LeftSuper;
            case VK_RWIN: return Key::RightSuper;
            default:
                break;
            }

            if (wParam >= '0' && wParam <= '9')
            {
                return static_cast<Key>(static_cast<std::uint16_t>(Key::Digit0)
                    + static_cast<std::uint16_t>(wParam - '0'));
            }
            if (wParam >= 'A' && wParam <= 'Z')
            {
                return static_cast<Key>(static_cast<std::uint16_t>(Key::A)
                    + static_cast<std::uint16_t>(wParam - 'A'));
            }
            if (wParam >= VK_F1 && wParam <= VK_F12)
            {
                return static_cast<Key>(static_cast<std::uint16_t>(Key::F1)
                    + static_cast<std::uint16_t>(wParam - VK_F1));
            }
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
            {
                return static_cast<Key>(static_cast<std::uint16_t>(Key::Keypad0)
                    + static_cast<std::uint16_t>(wParam - VK_NUMPAD0));
            }
            return Key::Unknown;
        }

        // **누른 순간의 조합키를 그때 읽는다.** 이벤트를 나중에 꺼내 볼 때 다시 물으면
        // 그 사이에 사용자가 손을 뗐을 수 있다.
        KeyModifiers CurrentModifiers()
        {
            KeyModifiers modifiers = KeyModifierNone;
            if (GetKeyState(VK_SHIFT) < 0)
            {
                modifiers |= KeyModifierShift;
            }
            if (GetKeyState(VK_CONTROL) < 0)
            {
                modifiers |= KeyModifierControl;
            }
            if (GetKeyState(VK_MENU) < 0)
            {
                modifiers |= KeyModifierAlt;
            }
            if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0)
            {
                modifiers |= KeyModifierSuper;
            }
            return modifiers;
        }

        LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_CLOSE)
            {
                // This slot belongs to our window class and only stores its close flag.
                SetWindowLongPtrW(window, GWLP_USERDATA, 1);
                return 0;
            }

            // 창 클래스의 여분 슬롯에 플랫폼을 적어 두었다. 창을 만든 직후에 넣으므로
            // 그 전에 오는 메시지(WM_CREATE 등)에는 없다 - 입력은 전부 그 뒤에 온다.
            auto* platform = reinterpret_cast<WindowsPlatform*>(
                GetWindowLongPtrW(window, PlatformSlot));
            if (platform == nullptr)
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }

            InputEvent event;
            event.modifiers = CurrentModifiers();
            switch (message)
            {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                event.kind = InputEventKind::KeyDown;
                event.key = TranslateKey(wParam, lParam);
                // 자동 반복은 30번 비트가 알려준다.
                event.repeat = (lParam & (1 << 30)) != 0;
                platform->RecordInputEvent(event);
                break;

            case WM_KEYUP:
            case WM_SYSKEYUP:
                event.kind = InputEventKind::KeyUp;
                event.key = TranslateKey(wParam, lParam);
                platform->RecordInputEvent(event);
                break;

            case WM_CHAR:
            case WM_SYSCHAR:
            {
                // UTF-16 이므로 서러게이트 쌍이 두 번에 나눠 온다. 앞쪽을 들고 있다가
                // 뒤쪽이 오면 합친다. 합치지 않으면 BMP 밖 글자가 깨진다.
                const auto unit = static_cast<std::uint16_t>(wParam);
                if (unit >= 0xD800 && unit <= 0xDBFF)
                {
                    platform->SetPendingHighSurrogate(unit);
                    break;
                }
                event.kind = InputEventKind::Text;
                const std::uint16_t high = platform->TakePendingHighSurrogate();
                if (high != 0 && unit >= 0xDC00 && unit <= 0xDFFF)
                {
                    event.codePoint = 0x10000u
                        + ((static_cast<std::uint32_t>(high) - 0xD800u) << 10)
                        + (static_cast<std::uint32_t>(unit) - 0xDC00u);
                }
                else
                {
                    event.codePoint = unit;
                }
                // 제어 문자는 글자가 아니다. Backspace 와 Enter 는 키 이벤트로 이미 갔다.
                if (event.codePoint >= 0x20 && event.codePoint != 0x7F)
                {
                    platform->RecordInputEvent(event);
                }
                break;
            }

            case WM_MOUSEMOVE:
                event.kind = InputEventKind::MouseMove;
                event.x = static_cast<float>(GET_X_LPARAM(lParam));
                event.y = static_cast<float>(GET_Y_LPARAM(lParam));
                platform->RecordInputEvent(event);
                break;

            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONDBLCLK:
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
            {
                const bool down = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK
                    || message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK
                    || message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK
                    || message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
                event.kind = down
                    ? InputEventKind::MouseButtonDown
                    : InputEventKind::MouseButtonUp;
                if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK
                    || message == WM_LBUTTONUP)
                {
                    event.button = MouseButton::Left;
                }
                else if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK
                    || message == WM_RBUTTONUP)
                {
                    event.button = MouseButton::Right;
                }
                else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK
                    || message == WM_MBUTTONUP)
                {
                    event.button = MouseButton::Middle;
                }
                else
                {
                    event.button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1
                        ? MouseButton::Extra1
                        : MouseButton::Extra2;
                }
                event.x = static_cast<float>(GET_X_LPARAM(lParam));
                event.y = static_cast<float>(GET_Y_LPARAM(lParam));
                platform->RecordInputEvent(event);
                break;
            }

            case WM_MOUSEWHEEL:
                event.kind = InputEventKind::MouseWheel;
                event.y = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam))
                    / static_cast<float>(WHEEL_DELTA);
                platform->RecordInputEvent(event);
                break;

            case WM_MOUSEHWHEEL:
                event.kind = InputEventKind::MouseWheel;
                event.x = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam))
                    / static_cast<float>(WHEEL_DELTA);
                platform->RecordInputEvent(event);
                break;

            case WM_SETFOCUS:
                event.kind = InputEventKind::FocusGained;
                platform->RecordInputEvent(event);
                break;

            case WM_KILLFOCUS:
                event.kind = InputEventKind::FocusLost;
                platform->RecordInputEvent(event);
                break;

            default:
                break;
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
        windowClass.cbWndExtra = sizeof(void*);

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

        SetWindowLongPtrW(window, PlatformSlot, reinterpret_cast<LONG_PTR>(this));

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
        // 지난 프레임 것을 버리고 다시 모은다. 꺼내 가지 않은 입력은 사라진다 -
        // 한 프레임을 통째로 건너뛴 쪽이 옛 입력을 뒤늦게 받는 것보다 낫다.
        m_inputEvents.Resize(0);
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

    JArrayView<InputEvent> WindowsPlatform::GetInputEvents() const
    {
        return {m_inputEvents.Data(), static_cast<std::uint32_t>(m_inputEvents.Size())};
    }

    void WindowsPlatform::RecordInputEvent(const InputEvent& event)
    {
        // 한 프레임에 이만큼 쌓일 일은 없다. 넘치면 버린다 - 무한히 자라는 것보다 낫다.
        if (m_inputEvents.Size() >= MaxInputEventsPerFrame)
        {
            return;
        }
        m_inputEvents.Add(event);
    }

    std::uint16_t WindowsPlatform::TakePendingHighSurrogate()
    {
        const std::uint16_t pending = m_pendingHighSurrogate;
        m_pendingHighSurrogate = 0;
        return pending;
    }

    void WindowsPlatform::SetPendingHighSurrogate(std::uint16_t unit)
    {
        m_pendingHighSurrogate = unit;
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
