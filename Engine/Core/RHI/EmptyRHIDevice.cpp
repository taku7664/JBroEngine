#include "pch.h"
#include "EmptyRHIDevice.h"

bool CEmptyRHIDevice::Initialize(const RHIDesc& desc)
{
	m_desc = desc;
	m_isInitialized = true;
	return true;
}

void CEmptyRHIDevice::BeginFrame()
{
}

void CEmptyRHIDevice::EndFrame()
{
}

void CEmptyRHIDevice::Finalize()
{
	m_isInitialized = false;
}

OwnerPtr<IRHIBuffer> CEmptyRHIDevice::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
	(void)desc;
	(void)initialData;
	return nullptr;
}

OwnerPtr<IRHITexture> CEmptyRHIDevice::CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData)
{
	(void)desc;
	(void)initialData;
	return nullptr;
}

OwnerPtr<IRHISampler> CEmptyRHIDevice::CreateSampler(const RHISamplerDesc& desc)
{
	(void)desc;
	return nullptr;
}

OwnerPtr<IRHIProgram> CEmptyRHIDevice::CreateProgram(const RHIProgramDesc& desc)
{
	(void)desc;
	return nullptr;
}

OwnerPtr<IRHIGraphicsPipeline> CEmptyRHIDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
	(void)desc;
	return nullptr;
}

SafePtr<IRHISwapchain> CEmptyRHIDevice::GetSwapchain() const
{
	return nullptr;
}

SafePtr<IRHICommandContext> CEmptyRHIDevice::GetImmediateCommandContext() const
{
	return nullptr;
}

RHINativeDeviceDesc CEmptyRHIDevice::GetNativeDeviceDesc() const
{
	return RHINativeDeviceDesc{};
}

ERHIApi CEmptyRHIDevice::GetApi() const
{
	return m_desc.Api;
}

const char* CEmptyRHIDevice::GetName() const
{
	return "EmptyRHI";
}
