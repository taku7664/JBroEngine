#include "pch.h"
#include "WindowsRenderSurface.h"

#include "Core/EngineCore.h"
#include "Core/Input/Input.h"

#if JBRO_PLATFORM_WINDOWS
#include <windowsx.h>

namespace
{
	EKeyCode ToKeyCode(WPARAM wParam)
	{
		if (wParam >= 'A' && wParam <= 'Z')
		{
			return static_cast<EKeyCode>(static_cast<int>(EKeyCode::A) + static_cast<int>(wParam - 'A'));
		}
		if (wParam >= '0' && wParam <= '9')
		{
			return static_cast<EKeyCode>(static_cast<int>(EKeyCode::Num0) + static_cast<int>(wParam - '0'));
		}

		switch (wParam)
		{
		case VK_ESCAPE: return EKeyCode::Escape;
		case VK_SPACE: return EKeyCode::Space;
		case VK_RETURN: return EKeyCode::Enter;
		case VK_TAB: return EKeyCode::Tab;
		case VK_BACK: return EKeyCode::Backspace;
		case VK_LEFT: return EKeyCode::Left;
		case VK_RIGHT: return EKeyCode::Right;
		case VK_UP: return EKeyCode::Up;
		case VK_DOWN: return EKeyCode::Down;
		default: return EKeyCode::Unknown;
		}
	}

	bool SubmitInputMessage(const InputMessage& message)
	{
		return Engine.Input ? Engine.Input->SubmitMessage(message) : true;
	}
}
#endif

bool CWindowsRenderSurface::Create(const RenderSurfaceCreateDesc& desc)
{
	m_desc = desc;

#if JBRO_PLATFORM_WINDOWS
	constexpr const wchar_t* WINDOW_CLASS_NAME = L"JBroEngineWindowClass";

	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEXW);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &CWindowsRenderSurface::WindowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.lpszClassName = WINDOW_CLASS_NAME;

	RegisterClassExW(&windowClass);

	DWORD style = WS_OVERLAPPEDWINDOW;
	if (false == desc.IsResizable)
	{
		style &= ~WS_THICKFRAME;
		style &= ~WS_MAXIMIZEBOX;
	}

	RECT windowRect = { 0, 0, desc.Width, desc.Height };
	AdjustWindowRect(&windowRect, style, FALSE);

	HWND hwnd = CreateWindowExW(
		0,
		WINDOW_CLASS_NAME,
		desc.Title,
		style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		GetModuleHandleW(nullptr),
		this
	);

	if (nullptr == hwnd)
	{
		return false;
	}

	m_nativeHandle = hwnd;
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
#endif

	m_isCreated = true;
	return true;
}

void CWindowsRenderSurface::Destroy()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_nativeHandle)
	{
		DestroyWindow(static_cast<HWND>(m_nativeHandle));
	}
#endif
	m_nativeHandle = nullptr;
	m_isCreated = false;
}

void CWindowsRenderSurface::PollEvents(PlatformEvent& platformEvent)
{
#if JBRO_PLATFORM_WINDOWS
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (WM_QUIT == msg.message)
		{
			platformEvent.WantsExit = true;
			continue;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// 포커스 전환은 WndProc(WM_ACTIVATEAPP)이 엣지로 잡아둔다. 메세지를 모두 drain 한 뒤
	// 여기서 1회씩 구독자에게 푸시한다(WndProc 깊은 곳 재진입 회피).
	if (m_focusGained)
	{
		m_focusGained = false;
		DispatchSurfaceEvent({ ESurfaceEventType::FocusGained });
	}
	if (m_focusLost)
	{
		m_focusLost = false;
		DispatchSurfaceEvent({ ESurfaceEventType::FocusLost });
	}
	if (m_resized)
	{
		m_resized = false;
		DispatchSurfaceEvent({ ESurfaceEventType::Resized, Size<int>(m_resizeWidth, m_resizeHeight) });
	}
#else
	(void)platformEvent;
#endif
}

RenderSurfaceSize CWindowsRenderSurface::GetSize() const
{
#if JBRO_PLATFORM_WINDOWS
	if (m_nativeHandle)
	{
		RECT clientRect = {};
		GetClientRect(static_cast<HWND>(m_nativeHandle), &clientRect);
		return RenderSurfaceSize{ clientRect.right - clientRect.left, clientRect.bottom - clientRect.top };
	}
#endif
	return RenderSurfaceSize{ m_desc.Width, m_desc.Height };
}

NativeSurfaceHandle CWindowsRenderSurface::GetNativeSurfaceHandle() const
{
	return NativeSurfaceHandle{ ERenderSurfaceType::Win32Hwnd, m_nativeHandle };
}

void CWindowsRenderSurface::SetNativeMessageHandler(NativeSurfaceMessageHandler handler)
{
	m_nativeMessageHandler = std::move(handler);
}

#if JBRO_PLATFORM_WINDOWS
LRESULT CALLBACK CWindowsRenderSurface::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	CWindowsRenderSurface* renderSurface = nullptr;
	if (WM_NCCREATE == message)
	{
		CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
		renderSurface = static_cast<CWindowsRenderSurface*>(createStruct ? createStruct->lpCreateParams : nullptr);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(renderSurface));
	}
	else
	{
		renderSurface = reinterpret_cast<CWindowsRenderSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}

	if (renderSurface && renderSurface->m_nativeMessageHandler)
	{
		std::intptr_t result = 0;
		NativeSurfaceMessage nativeMessage;
		nativeMessage.SurfaceHandle = hwnd;
		nativeMessage.Message = message;
		nativeMessage.WParam = static_cast<std::uintptr_t>(wParam);
		nativeMessage.LParam = static_cast<std::intptr_t>(lParam);
		if (renderSurface->m_nativeMessageHandler(nativeMessage, result))
		{
			return static_cast<LRESULT>(result);
		}
	}

	switch (message)
	{
	case WM_ACTIVATEAPP:
	{
		const bool isFocused = (FALSE != wParam);
		if (renderSurface && renderSurface->m_isFocused != isFocused)
		{
			renderSurface->m_isFocused = isFocused;
			renderSurface->m_focusGained = isFocused;
			renderSurface->m_focusLost = !isFocused;
		}
		break;
	}
	case WM_SIZE:
	{
		// 최소화(SIZE_MINIMIZED, 0x1)는 클라이언트 0 → 무시. 그 외 크기 변화만 엣지로 기록.
		if (renderSurface && SIZE_MINIMIZED != wParam)
		{
			const int width  = static_cast<int>(LOWORD(lParam));
			const int height = static_cast<int>(HIWORD(lParam));
			if (width > 0 && height > 0)
			{
				renderSurface->m_resized = true;
				renderSurface->m_resizeWidth = width;
				renderSurface->m_resizeHeight = height;
			}
		}
		break;
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		InputMessage inputMessage;
		inputMessage.Type = EInputMessageType::KeyDown;
		inputMessage.Key = ToKeyCode(wParam);
		if (false == SubmitInputMessage(inputMessage))
		{
			return 0;
		}
		break;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		InputMessage inputMessage;
		inputMessage.Type = EInputMessageType::KeyUp;
		inputMessage.Key = ToKeyCode(wParam);
		if (false == SubmitInputMessage(inputMessage))
		{
			return 0;
		}
		break;
	}
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	{
		InputMessage inputMessage;
		inputMessage.Type = EInputMessageType::MouseDown;
		inputMessage.MouseButton = WM_RBUTTONDOWN == message ? EMouseButton::Right : (WM_MBUTTONDOWN == message ? EMouseButton::Middle : EMouseButton::Left);
		inputMessage.MouseX = GET_X_LPARAM(lParam);
		inputMessage.MouseY = GET_Y_LPARAM(lParam);
		if (false == SubmitInputMessage(inputMessage))
		{
			return 0;
		}
		break;
	}
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	{
		InputMessage inputMessage;
		inputMessage.Type = EInputMessageType::MouseUp;
		inputMessage.MouseButton = WM_RBUTTONUP == message ? EMouseButton::Right : (WM_MBUTTONUP == message ? EMouseButton::Middle : EMouseButton::Left);
		inputMessage.MouseX = GET_X_LPARAM(lParam);
		inputMessage.MouseY = GET_Y_LPARAM(lParam);
		if (false == SubmitInputMessage(inputMessage))
		{
			return 0;
		}
		break;
	}
	case WM_MOUSEMOVE:
	{
		InputMessage inputMessage;
		inputMessage.Type = EInputMessageType::MouseMove;
		inputMessage.MouseX = GET_X_LPARAM(lParam);
		inputMessage.MouseY = GET_Y_LPARAM(lParam);
		if (false == SubmitInputMessage(inputMessage))
		{
			return 0;
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		InputMessage inputMessage;
		inputMessage.Type = EInputMessageType::MouseWheel;
		inputMessage.WheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
		if (false == SubmitInputMessage(inputMessage))
		{
			return 0;
		}
		break;
	}
	default:
		break;
	}

	if (WM_CLOSE == message)
	{
		DestroyWindow(hwnd);
		return 0;
	}

	if (WM_DESTROY == message)
	{
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hwnd, message, wParam, lParam);
}
#endif
