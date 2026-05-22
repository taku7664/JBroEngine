#include "pch.h"
#include "WindowsPlatform.h"

#include "Core/Platform/Windows/WindowsRenderSurface.h"

bool CWindowsPlatform::Initialize(const PlatformDesc& desc)
{
	m_desc = desc;

	RenderSurfaceCreateDesc renderSurfaceDesc;
	renderSurfaceDesc.Title = desc.ApplicationName;
	renderSurfaceDesc.Width = desc.WindowWidth;
	renderSurfaceDesc.Height = desc.WindowHeight;

	m_mainRenderSurface = MakeOwnerPtr<CWindowsRenderSurface>();
	if (!m_mainRenderSurface || false == m_mainRenderSurface->Create(renderSurfaceDesc))
	{
		return false;
	}

	m_isInitialized = true;
	return true;
}

void CWindowsPlatform::PollEvents(PlatformEvent& platformEvent)
{
	if (m_mainRenderSurface)
	{
		m_mainRenderSurface->PollEvents(platformEvent);
	}
}

void CWindowsPlatform::Finalize()
{
	if (m_mainRenderSurface)
	{
		m_mainRenderSurface->Destroy();
		m_mainRenderSurface.Reset();
	}

	m_isInitialized = false;
}

SafePtr<IRenderSurface> CWindowsPlatform::GetMainRenderSurface() const
{
	return m_mainRenderSurface.GetSafePtr();
}

EPlatformType CWindowsPlatform::GetPlatformType() const
{
	return EPlatformType::Windows;
}

const wchar_t* CWindowsPlatform::GetName() const
{
	return L"Windows";
}
