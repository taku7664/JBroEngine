#include "pch.h"
#include "WebCanvasSurface.h"

namespace
{
	constexpr const char* WEB_CANVAS_SELECTOR = "#jbro-canvas";
}

bool CWebCanvasSurface::Create(const RenderSurfaceCreateDesc& desc)
{
	m_desc = desc;
	m_canvasHandle = const_cast<char*>(WEB_CANVAS_SELECTOR);
	m_isCreated = true;
	return true;
}

void CWebCanvasSurface::Destroy()
{
	m_canvasHandle = nullptr;
	m_isCreated = false;
}

void CWebCanvasSurface::PollEvents(PlatformEvent& platformEvent)
{
	platformEvent.IsFocused = true;
	platformEvent.FocusGained = false;
	platformEvent.FocusLost = false;
}

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
