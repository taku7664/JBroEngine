#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHIDevice.h"

#if JBRO_PLATFORM_WINDOWS
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
#endif

class CD3D11RHIDevice final : public IRHIDevice
{
public:
	bool Initialize(const RHIDesc& desc) override;
	void BeginFrame() override;
	void EndFrame() override;
	void Finalize() override;
	void HandleSurfaceResize(const RenderSurfaceSize& size) override;

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

#if JBRO_PLATFORM_WINDOWS
	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_deviceContext = nullptr;
	IDXGISwapChain* m_swapchain = nullptr;
#endif
};
