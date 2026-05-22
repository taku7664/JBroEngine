#include "pch.h"
#include "WebPlatform.h"

#include "Core/Platform/Web/WebCanvasSurface.h"

bool CWebPlatform::Initialize(const PlatformDesc& desc)
{
	m_desc = desc;

	RenderSurfaceCreateDesc renderSurfaceDesc;
	renderSurfaceDesc.Title = desc.ApplicationName;
	renderSurfaceDesc.Width = desc.WindowWidth;
	renderSurfaceDesc.Height = desc.WindowHeight;

	m_mainRenderSurface = MakeOwnerPtr<CWebCanvasSurface>();
	if (!m_mainRenderSurface || false == m_mainRenderSurface->Create(renderSurfaceDesc))
	{
		return false;
	}

	m_isInitialized = true;
	return true;
}

void CWebPlatform::PollEvents(PlatformEvent& platformEvent)
{
	if (m_mainRenderSurface)
	{
		m_mainRenderSurface->PollEvents(platformEvent);
	}
}

void CWebPlatform::Finalize()
{
	if (m_mainRenderSurface)
	{
		m_mainRenderSurface->Destroy();
		m_mainRenderSurface.Reset();
	}

	m_isInitialized = false;
}

SafePtr<IRenderSurface> CWebPlatform::GetMainRenderSurface() const
{
	return m_mainRenderSurface.GetSafePtr();
}

EPlatformType CWebPlatform::GetPlatformType() const
{
	return EPlatformType::Web;
}

const wchar_t* CWebPlatform::GetName() const
{
	return L"Web";
}
