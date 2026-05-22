#pragma once

#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPURHIDevice final : public IRHIDevice
{
public:
	bool Initialize(const RHIDesc& desc) override;
	void BeginFrame() override;
	void EndFrame() override;
	void Finalize() override;

	OwnerPtr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc, const void* initialData) override;
	OwnerPtr<IRHITexture> CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData) override;
	OwnerPtr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) override;
	OwnerPtr<IRHIProgram> CreateProgram(const RHIProgramDesc& desc) override;
	OwnerPtr<IRHIGraphicsPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;

	SafePtr<IRHISwapchain> GetSwapchain() const override;
	SafePtr<IRHICommandContext> GetImmediateCommandContext() const override;
	RHINativeDeviceDesc GetNativeDeviceDesc() const override;

	ERHIApi GetApi() const override;
	const char* GetName() const override;

private:
	RHIDesc m_desc;
	OwnerPtr<IRHISwapchain> m_rhiSwapchain;
	OwnerPtr<IRHICommandContext> m_immediateCommandContext;
	bool m_isInitialized = false;
#if JBRO_PLATFORM_WEB
	WGPUInstance m_instance = nullptr;
	WGPUDevice m_device = nullptr;
	WGPUQueue m_queue = nullptr;
	WGPUSurface m_surface = nullptr;
#endif
};
