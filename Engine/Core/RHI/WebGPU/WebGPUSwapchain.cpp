#include "pch.h"
#include "WebGPUSwapchain.h"

bool CWebGPUSwapchain::Initialize(const RenderSurfaceDesc& surfaceDesc)
{
	m_surfaceDesc = surfaceDesc;
	return true;
}

void CWebGPUSwapchain::Resize(const RenderSurfaceSize& size)
{
	m_surfaceDesc.Size = size;
#if JBRO_PLATFORM_WEB
	ConfigureSurface();
#endif
}

void CWebGPUSwapchain::Present()
{
#if JBRO_PLATFORM_WEB
	if (m_surface)
	{
		wgpuSurfacePresent(m_surface);
	}
	ReleaseCurrentTexture();
#endif
}

RenderSurfaceSize CWebGPUSwapchain::GetSize() const
{
	return m_surfaceDesc.Size;
}

void CWebGPUSwapchain::Finalize()
{
#if JBRO_PLATFORM_WEB
	ReleaseCurrentTexture();
	if (m_surface)
	{
		wgpuSurfaceRelease(m_surface);
		m_surface = nullptr;
	}
	m_device = nullptr;
#endif
}

#if JBRO_PLATFORM_WEB
bool CWebGPUSwapchain::BindNativeSurface(WGPUDevice device, WGPUSurface surface, WGPUTextureFormat format)
{
	if (nullptr == device || nullptr == surface)
	{
		return false;
	}

	m_device = device;
	m_surface = surface;
	m_format = format;
	ConfigureSurface();
	return true;
}

WGPUTextureView CWebGPUSwapchain::AcquireCurrentTextureView()
{
	if (m_currentTextureView)
	{
		return m_currentTextureView;
	}

	if (nullptr == m_surface)
	{
		return nullptr;
	}

	WGPUSurfaceTexture surfaceTexture = {};
	wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
	if (nullptr == surfaceTexture.texture)
	{
		return nullptr;
	}

	m_currentTexture = surfaceTexture.texture;
	WGPUTextureViewDescriptor viewDesc = {};
	m_currentTextureView = wgpuTextureCreateView(m_currentTexture, &viewDesc);
	return m_currentTextureView;
}

WGPUTextureFormat CWebGPUSwapchain::GetFormat() const
{
	return m_format;
}

void CWebGPUSwapchain::ConfigureSurface()
{
	if (nullptr == m_device || nullptr == m_surface || m_surfaceDesc.Size.Width <= 0 || m_surfaceDesc.Size.Height <= 0)
	{
		return;
	}

	WGPUSurfaceConfiguration config = {};
	config.device = m_device;
	config.format = m_format;
	config.usage = WGPUTextureUsage_RenderAttachment;
	config.width = static_cast<std::uint32_t>(m_surfaceDesc.Size.Width);
	config.height = static_cast<std::uint32_t>(m_surfaceDesc.Size.Height);
	config.presentMode = WGPUPresentMode_Fifo;
	config.alphaMode = WGPUCompositeAlphaMode_Auto;
	wgpuSurfaceConfigure(m_surface, &config);
}

void CWebGPUSwapchain::ReleaseCurrentTexture()
{
	if (m_currentTextureView)
	{
		wgpuTextureViewRelease(m_currentTextureView);
		m_currentTextureView = nullptr;
	}
	if (m_currentTexture)
	{
		wgpuTextureRelease(m_currentTexture);
		m_currentTexture = nullptr;
	}
}
#endif

