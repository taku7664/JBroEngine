#include "pch.h"
#include "MobileRenderSurface.h"

#include <utility>

bool CMobileRenderSurface::Create(const RenderSurfaceCreateDesc& desc)
{
	m_desc = desc;
	m_isFocused = true;
	m_focusGained = false;
	m_focusLost = false;
	m_isCreated = true;
	return true;
}

void CMobileRenderSurface::Destroy()
{
	m_nativeHandle = nullptr;
	m_nativeMessageHandler = nullptr;
	m_isCreated = false;
}

void CMobileRenderSurface::PollEvents(PlatformEvent& platformEvent)
{
	(void)platformEvent;

	// 포커스 전환 엣지를 구독자에게 1회씩 푸시.
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

RenderSurfaceSize CMobileRenderSurface::GetSize() const
{
	return RenderSurfaceSize{ m_desc.Width, m_desc.Height };
}

NativeSurfaceHandle CMobileRenderSurface::GetNativeSurfaceHandle() const
{
	return NativeSurfaceHandle{ ERenderSurfaceType::MobileNativeSurface, m_nativeHandle };
}

void CMobileRenderSurface::SetNativeMessageHandler(NativeSurfaceMessageHandler handler)
{
	m_nativeMessageHandler = std::move(handler);
}

void CMobileRenderSurface::SetNativeSurfaceHandle(void* handle)
{
	m_nativeHandle = handle;
}

void CMobileRenderSurface::SetSize(int width, int height)
{
	if (m_desc.Width == width && m_desc.Height == height)
	{
		return;
	}
	m_desc.Width = width;
	m_desc.Height = height;
	if (width > 0 && height > 0)
	{
		m_resized = true;
		m_resizeWidth = width;
		m_resizeHeight = height;
	}
}

void CMobileRenderSurface::SetFocus(bool isFocused)
{
	if (m_isFocused == isFocused)
	{
		return;
	}

	m_isFocused = isFocused;
	m_focusGained = isFocused;
	m_focusLost = false == isFocused;
}
