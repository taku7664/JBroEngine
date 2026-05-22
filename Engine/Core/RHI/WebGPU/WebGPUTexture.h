#pragma once

#include "Core/RHI/IRHITexture.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPUTexture final : public IRHITexture
{
public:
	explicit CWebGPUTexture(const RHITexture2DDesc& desc);
	~CWebGPUTexture() override;

	const RHITexture2DDesc& GetDesc() const override;
	RHITextureNativeHandle GetNativeHandle() const override;

#if JBRO_PLATFORM_WEB
	void BindNativeTexture(WGPUTexture texture, WGPUTextureView textureView);
	WGPUTexture GetNativeTexture() const;
	WGPUTextureView GetTextureView() const;
#endif

private:
	RHITexture2DDesc m_desc;
#if JBRO_PLATFORM_WEB
	WGPUTexture m_texture = nullptr;
	WGPUTextureView m_textureView = nullptr;
#endif
};

