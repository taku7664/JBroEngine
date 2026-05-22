#pragma once

#include "Core/RHI/RHITypes.h"
#include "Core/RHI/RHIGraphicsTypes.h"

class IRHISwapchain;
class IRHICommandContext;
class IRHIBuffer;
class IRHIProgram;
class IRHIGraphicsPipeline;
class IRHITexture;
class IRHISampler;

class IRHIDevice : public EnableSafeFromThis<IRHIDevice>
{
public:
	virtual ~IRHIDevice() = default;

public:
	virtual bool Initialize(const RHIDesc& desc) = 0;
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void Finalize() = 0;

	virtual OwnerPtr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc, const void* initialData) = 0;
	virtual OwnerPtr<IRHITexture> CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData) = 0;
	virtual OwnerPtr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) = 0;
	virtual OwnerPtr<IRHIProgram> CreateProgram(const RHIProgramDesc& desc) = 0;
	virtual OwnerPtr<IRHIGraphicsPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;

	virtual SafePtr<IRHISwapchain> GetSwapchain() const = 0;
	virtual SafePtr<IRHICommandContext> GetImmediateCommandContext() const = 0;
	virtual RHINativeDeviceDesc GetNativeDeviceDesc() const = 0;

	virtual ERHIApi GetApi() const = 0;
	virtual const char* GetName() const = 0;
};
