#include "pch.h"
#include "WebCanvasSurface.h"

#if JBRO_PLATFORM_WEB
#include <emscripten/html5.h>
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
}

#if JBRO_PLATFORM_WEB
int CWebCanvasSurface::OnVisibilityChange(int /*eventType*/, const EmscriptenVisibilityChangeEvent* event, void* userData)
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

int CWebCanvasSurface::OnCanvasResize(int /*eventType*/, const EmscriptenUiEvent* /*event*/, void* userData)
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
	}
	return EM_TRUE;
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
