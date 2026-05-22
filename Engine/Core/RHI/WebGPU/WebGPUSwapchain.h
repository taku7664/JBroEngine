#pragma once

#include "Core/RHI/IRHISwapchain.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPUSwapchain final : public IRHISwapchain
{
public:
	bool Initialize(const RenderSurfaceDesc& surfaceDesc) override;
	void Resize(const RenderSurfaceSize& size) override;
	void Present() override;
	RenderSurfaceSize GetSize() const override;

	void Finalize();

#if JBRO_PLATFORM_WEB
	bool BindNativeSurface(WGPUDevice device, WGPUSurface surface, WGPUTextureFormat format);
	WGPUTextureView AcquireCurrentTextureView();
	WGPUTextureFormat GetFormat() const;
#endif

private:
#if JBRO_PLATFORM_WEB
	void ConfigureSurface();
	void ReleaseCurrentTexture();
#endif

private:
	RenderSurfaceDesc m_surfaceDesc;
#if JBRO_PLATFORM_WEB
	WGPUDevice m_device = nullptr;
	WGPUSurface m_surface = nullptr;
	WGPUTextureFormat m_format = WGPUTextureFormat_BGRA8Unorm;
	WGPUTexture m_currentTexture = nullptr;
	WGPUTextureView m_currentTextureView = nullptr;
#endif
};

