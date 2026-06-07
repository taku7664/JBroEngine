#include "pch.h"
#include "WindowsRenderSurface.h"

#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"

#if JBRO_PLATFORM_WINDOWS
#include <windowsx.h>

namespace
{
	// 휠은 "현재 상태"가 없는 누적 신호 → 폴링 불가. 메시지로 받아 InputSystem 에 누적만 한다.
	// (키/마우스버튼/위치는 InputSystem 이 매 프레임 직접 폴링 — 메시지 사용 안 함.)
	void AccumulateWheel(float delta)
	{
		if (Engine.InputSystem)
		{
			Engine.InputSystem->AccumulateWheel(delta);
		}
	}

	// 텍스트 입력 — WM_CHAR 의 완성 코드포인트를 InputSystem 에 누적(폴링 불가 신호).
	void AccumulateText(char32_t codepoint)
	{
		if (Engine.InputSystem)
		{
			Engine.InputSystem->AccumulateText(codepoint);
		}
	}

	// 터치 — WM_POINTER* 메시지를 InputSystem 에 누적(Win8+ 통합 포인터). 마우스는 직접 폴링이라 제외.
	// 좌표는 스크린 → 클라이언트 변환. Ended/Cancelled 는 정보 조회 실패해도 슬롯 해제 위해 전달.
	void AccumulateTouchPointer(HWND hwnd, WPARAM wParam, ETouchPhase phase)
	{
		if (nullptr == Engine.InputSystem)
		{
			return;
		}
		const std::uint32_t pointerId = GET_POINTERID_WPARAM(wParam);
		POINTER_INFO info = {};
		int x = 0;
		int y = 0;
		if (GetPointerInfo(pointerId, &info))
		{
			if (PT_TOUCH != info.pointerType && PT_PEN != info.pointerType)
			{
				return; // 마우스 포인터는 무시(마우스는 매 프레임 폴링)
			}
			POINT pt = info.ptPixelLocation; // 스크린 좌표
			ScreenToClient(hwnd, &pt);
			x = static_cast<int>(pt.x);
			y = static_cast<int>(pt.y);
		}
		else if (ETouchPhase::Ended != phase && ETouchPhase::Cancelled != phase)
		{
			return; // Began/Moved 인데 포인터 정보 못 얻으면 무시
		}
		Engine.InputSystem->AccumulateTouch(static_cast<std::int32_t>(pointerId), x, y, phase);
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
	case WM_MOUSEWHEEL:
	{
		// 폴링 불가 신호 → 누적. 키/마우스버튼/위치는 InputSystem 이 매 프레임 폴링한다.
		AccumulateWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));
		break;
	}
	case WM_CHAR:
	{
		// 텍스트 입력 — UTF-16 코드 유닛. 상위/하위 서로게이트를 결합해 완성 코드포인트로 누적.
		// (입력 스레드 단일 → 함수 로컬 static 으로 상위 서로게이트 상태 유지.)
		static wchar_t s_highSurrogate = 0;
		const wchar_t unit = static_cast<wchar_t>(wParam);
		if (unit >= 0xD800 && unit <= 0xDBFF)
		{
			s_highSurrogate = unit; // 상위 서로게이트 — 다음 코드 유닛과 결합 대기
		}
		else
		{
			char32_t codepoint;
			if (unit >= 0xDC00 && unit <= 0xDFFF && 0 != s_highSurrogate)
			{
				codepoint = 0x10000
					+ ((static_cast<char32_t>(s_highSurrogate) - 0xD800) << 10)
					+ (static_cast<char32_t>(unit) - 0xDC00);
			}
			else
			{
				codepoint = static_cast<char32_t>(unit); // BMP 단일 유닛(또는 짝 없는 하위 서로게이트)
			}
			s_highSurrogate = 0;
			AccumulateText(codepoint);
		}
		break;
	}
	case WM_POINTERDOWN:
		AccumulateTouchPointer(hwnd, wParam, ETouchPhase::Began);
		break;
	case WM_POINTERUPDATE:
		AccumulateTouchPointer(hwnd, wParam, ETouchPhase::Moved);
		break;
	case WM_POINTERUP:
		AccumulateTouchPointer(hwnd, wParam, ETouchPhase::Ended);
		break;
	case WM_POINTERCAPTURECHANGED:
		AccumulateTouchPointer(hwnd, wParam, ETouchPhase::Cancelled);
		break;
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
