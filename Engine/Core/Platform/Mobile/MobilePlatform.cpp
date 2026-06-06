#include "pch.h"
#include "MobilePlatform.h"

#include "Core/Platform/Mobile/MobileRenderSurface.h"

bool CMobilePlatform::Initialize(const PlatformDesc& desc)
{
	m_desc = desc;

	RenderSurfaceCreateDesc renderSurfaceDesc;
	renderSurfaceDesc.Title = desc.ApplicationName;
	renderSurfaceDesc.Width = desc.WindowWidth;
	renderSurfaceDesc.Height = desc.WindowHeight;
	renderSurfaceDesc.IsResizable = true;

	m_mainRenderSurface = MakeOwnerPtr<CMobileRenderSurface>();
	if (!m_mainRenderSurface || false == m_mainRenderSurface->Create(renderSurfaceDesc))
	{
		return false;
	}

	m_isInitialized = true;
	return true;
}

void CMobilePlatform::PollEvents(PlatformEvent& platformEvent)
{
	if (m_mainRenderSurface)
	{
		m_mainRenderSurface->PollEvents(platformEvent);
	}
	platformEvent.WantsExit = m_wantsExit;
}

void CMobilePlatform::Finalize()
{
	if (m_mainRenderSurface)
	{
		m_mainRenderSurface->Destroy();
		m_mainRenderSurface.Reset();
	}

	m_isInitialized = false;
}

SafePtr<IRenderSurface> CMobilePlatform::GetMainRenderSurface() const
{
	return m_mainRenderSurface.GetSafePtr();
}

EPlatformType CMobilePlatform::GetPlatformType() const
{
#if JBRO_PLATFORM_ANDROID
	return EPlatformType::Android;
#elif JBRO_PLATFORM_IOS
	return EPlatformType::IOS;
#else
	return EPlatformType::Unknown;
#endif
}

const wchar_t* CMobilePlatform::GetName() const
{
#if JBRO_PLATFORM_ANDROID
	return L"Android";
#elif JBRO_PLATFORM_IOS
	return L"iOS";
#else
	return L"Mobile";
#endif
}

void CMobilePlatform::RequestExit()
{
	m_wantsExit = true;
}

void CMobilePlatform::SetFocus(bool isFocused)
{
	if (CMobileRenderSurface* surface = static_cast<CMobileRenderSurface*>(m_mainRenderSurface.Get()))
	{
		surface->SetFocus(isFocused);
	}
}

void CMobilePlatform::SetNativeSurfaceHandle(void* handle)
{
	if (CMobileRenderSurface* surface = static_cast<CMobileRenderSurface*>(m_mainRenderSurface.Get()))
	{
		surface->SetNativeSurfaceHandle(handle);
	}
}

void CMobilePlatform::ResizeSurface(int width, int height)
{
	if (CMobileRenderSurface* surface = static_cast<CMobileRenderSurface*>(m_mainRenderSurface.Get()))
	{
		surface->SetSize(width, height);
	}
}

void CMobilePlatform::NotifyPause()
{
	m_isPaused = true;
	SetFocus(false);
}

void CMobilePlatform::NotifyResume()
{
	m_isPaused = false;
	SetFocus(true);
}
