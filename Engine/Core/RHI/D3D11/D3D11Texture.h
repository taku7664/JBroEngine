#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHITexture.h"

#if JBRO_PLATFORM_WINDOWS
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
#endif

class CD3D11Texture final : public IRHITexture
{
public:
	CD3D11Texture(const RHITexture2DDesc& desc);
	~CD3D11Texture() override;

	const RHITexture2DDesc& GetDesc() const override;
	RHITextureNativeHandle GetNativeHandle() const override;

#if JBRO_PLATFORM_WINDOWS
	void BindNativeTexture(ID3D11Texture2D* texture, ID3D11ShaderResourceView* shaderResourceView, ID3D11RenderTargetView* renderTargetView);
	ID3D11ShaderResourceView* GetShaderResourceView() const;
	ID3D11RenderTargetView* GetRenderTargetView() const;
#endif

private:
	RHITexture2DDesc m_desc;
#if JBRO_PLATFORM_WINDOWS
	ID3D11Texture2D* m_texture = nullptr;
	ID3D11ShaderResourceView* m_shaderResourceView = nullptr;
	ID3D11RenderTargetView* m_renderTargetView = nullptr;
#endif
};
