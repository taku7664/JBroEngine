#include "pch.h"
#include "WebCanvasSurface.h"

#if JBRO_PLATFORM_WEB
#include <emscripten/html5.h>

#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#endif

namespace
{
	constexpr const char* WEB_CANVAS_SELECTOR = "#jbro-canvas";
}

bool CWebCanvasSurface::Create(const RenderSurfaceCreateDesc& desc)
{
	m_desc = desc;
	m_canvasHandle = const_cast<char*>(WEB_CANVAS_SELECTOR);
	m_isCreated = true;

#if JBRO_PLATFORM_WEB
	// 브라우저 가시성(탭 활성/비활성) = 앱 포커스, 윈도우 리사이즈 = 캔버스 크기 변경.
	emscripten_set_visibilitychange_callback(this, EM_FALSE, &CWebCanvasSurface::OnVisibilityChange);
	emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE, &CWebCanvasSurface::OnCanvasResize);
#endif
	return true;
}

void CWebCanvasSurface::Destroy()
{
	m_canvasHandle = nullptr;
	m_isCreated = false;
}

void CWebCanvasSurface::PollEvents(PlatformEvent& platformEvent)
{
	(void)platformEvent;

	// Emscripten 콜백이 비동기로 세팅한 엣지를 메인 루프에서 1회씩 디스패치(스레딩/재진입 통제).
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

#if JBRO_PLATFORM_WEB
	if (m_gameSurfaceRectPending)
	{
		UpdateGameSurfaceRect();
	}
#endif
}

#if JBRO_PLATFORM_WEB
EM_BOOL CWebCanvasSurface::OnVisibilityChange(int /*eventType*/, const EmscriptenVisibilityChangeEvent* event, void* userData)
{
	CWebCanvasSurface* self = static_cast<CWebCanvasSurface*>(userData);
	if (nullptr == self || nullptr == event)
	{
		return EM_FALSE;
	}
	const bool focused = (0 == event->hidden);
	if (self->m_isFocused != focused)
	{
		self->m_isFocused = focused;
		self->m_focusGained = focused;
		self->m_focusLost = (false == focused);
	}
	return EM_TRUE;
}

EM_BOOL CWebCanvasSurface::OnCanvasResize(int /*eventType*/, const EmscriptenUiEvent* /*event*/, void* userData)
{
	CWebCanvasSurface* self = static_cast<CWebCanvasSurface*>(userData);
	if (nullptr == self)
	{
		return EM_FALSE;
	}
	int width = 0;
	int height = 0;
	emscripten_get_canvas_element_size(WEB_CANVAS_SELECTOR, &width, &height);
	if (width > 0 && height > 0)
	{
		self->m_resized = true;
		self->m_resizeWidth = width;
		self->m_resizeHeight = height;
		// GetSize() 가 최신 캔버스 크기를 반환하도록 desc 도 갱신한다(윈도우의 GetClientRect 동등).
		self->m_desc.Width = width;
		self->m_desc.Height = height;
	}
	// 표시 크기가 달라졌으니 마우스 환산도 다시 잡는다(셸의 레터박스가 CSS 크기를 바꾼다).
	self->m_gameSurfaceRectPending = true;
	return EM_TRUE;
}

void CWebCanvasSurface::UpdateGameSurfaceRect()
{
	if (false == Engine.InputSystem.IsValid())
	{
		return; // 아직 입력 시스템이 없다 — 다음 프레임에 다시 시도한다.
	}

	double cssWidth = 0.0;
	double cssHeight = 0.0;
	if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_get_element_css_size(WEB_CANVAS_SELECTOR, &cssWidth, &cssHeight))
	{
		return;
	}

	int backingWidth = 0;
	int backingHeight = 0;
	emscripten_get_canvas_element_size(WEB_CANVAS_SELECTOR, &backingWidth, &backingHeight);
	if (cssWidth <= 0.0 || cssHeight <= 0.0 || backingWidth <= 0 || backingHeight <= 0)
	{
		return; // 아직 어느 쪽 크기도 확정되지 않았다.
	}

	// 마우스 콜백이 캔버스 기준 CSS 좌표를 주므로 원점은 0 이다 — 크기 환산만 하면 된다.
	Engine.InputSystem->SetGameSurfaceRect(
		0.0f, 0.0f,
		static_cast<float>(cssWidth), static_cast<float>(cssHeight),
		static_cast<float>(backingWidth), static_cast<float>(backingHeight));
	m_gameSurfaceRectPending = false;
}
#endif

RenderSurfaceSize CWebCanvasSurface::GetSize() const
{
	return RenderSurfaceSize{ m_desc.Width, m_desc.Height };
}

NativeSurfaceHandle CWebCanvasSurface::GetNativeSurfaceHandle() const
{
	return NativeSurfaceHandle{ ERenderSurfaceType::HtmlCanvas, m_canvasHandle };
}

void CWebCanvasSurface::SetNativeMessageHandler(NativeSurfaceMessageHandler handler)
{
	(void)handler;
}
