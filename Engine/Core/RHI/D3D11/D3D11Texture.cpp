#include "pch.h"
#include "D3D11Texture.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#endif

CD3D11Texture::CD3D11Texture(const RHITexture2DDesc& desc)
	: m_desc(desc)
{
}

CD3D11Texture::~CD3D11Texture()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_renderTargetView)
	{
		m_renderTargetView->Release();
		m_renderTargetView = nullptr;
	}
	if (m_shaderResourceView)
	{
		m_shaderResourceView->Release();
		m_shaderResourceView = nullptr;
	}
	if (m_texture)
	{
		m_texture->Release();
		m_texture = nullptr;
	}
#endif
}

const RHITexture2DDesc& CD3D11Texture::GetDesc() const
{
	return m_desc;
}

RHITextureNativeHandle CD3D11Texture::GetNativeHandle() const
{
	RHITextureNativeHandle handle;
#if JBRO_PLATFORM_WINDOWS
	handle.Texture = m_texture;
	handle.ShaderResourceView = m_shaderResourceView;
	handle.RenderTargetView = m_renderTargetView;
#endif
	return handle;
}

#if JBRO_PLATFORM_WINDOWS
void CD3D11Texture::BindNativeTexture(ID3D11Texture2D* texture, ID3D11ShaderResourceView* shaderResourceView, ID3D11RenderTargetView* renderTargetView)
{
	m_texture = texture;
	m_shaderResourceView = shaderResourceView;
	m_renderTargetView = renderTargetView;
}

ID3D11ShaderResourceView* CD3D11Texture::GetShaderResourceView() const
{
	return m_shaderResourceView;
}

ID3D11RenderTargetView* CD3D11Texture::GetRenderTargetView() const
{
	return m_renderTargetView;
}
#endif
