#include "pch.h"
#include "WebGPUTexture.h"

CWebGPUTexture::CWebGPUTexture(const RHITexture2DDesc& desc)
	: m_desc(desc)
{
}

CWebGPUTexture::~CWebGPUTexture()
{
#if JBRO_PLATFORM_WEB
	if (m_textureView)
	{
		wgpuTextureViewRelease(m_textureView);
		m_textureView = nullptr;
	}
	if (m_texture)
	{
		wgpuTextureRelease(m_texture);
		m_texture = nullptr;
	}
#endif
}

const RHITexture2DDesc& CWebGPUTexture::GetDesc() const
{
	return m_desc;
}

RHITextureNativeHandle CWebGPUTexture::GetNativeHandle() const
{
	RHITextureNativeHandle handle;
#if JBRO_PLATFORM_WEB
	handle.Texture = m_texture;
	handle.ShaderResourceView = m_textureView;
	handle.RenderTargetView = m_textureView;
#endif
	return handle;
}

#if JBRO_PLATFORM_WEB
void CWebGPUTexture::BindNativeTexture(WGPUTexture texture, WGPUTextureView textureView)
{
	m_texture = texture;
	m_textureView = textureView;
}

WGPUTexture CWebGPUTexture::GetNativeTexture() const
{
	return m_texture;
}

WGPUTextureView CWebGPUTexture::GetTextureView() const
{
	return m_textureView;
}
#endif

