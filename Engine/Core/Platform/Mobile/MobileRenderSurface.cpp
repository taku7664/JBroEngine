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
	platformEvent.IsFocused = m_isFocused;
	platformEvent.FocusGained = m_focusGained;
	platformEvent.FocusLost = m_focusLost;
	m_focusGained = false;
	m_focusLost = false;
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
	m_desc.Width = width;
	m_desc.Height = height;
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
