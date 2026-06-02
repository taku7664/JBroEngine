#include "pch.h"
#include "WebGPUCommandContext.h"

#include "Core/RHI/WebGPU/WebGPUBuffer.h"
#include "Core/RHI/WebGPU/WebGPUGraphicsPipeline.h"
#include "Core/RHI/WebGPU/WebGPUSampler.h"
#include "Core/RHI/WebGPU/WebGPUSwapchain.h"
#include "Core/RHI/WebGPU/WebGPUTexture.h"

#if JBRO_PLATFORM_WEB
void CWebGPUCommandContext::BindNativeContext(WGPUDevice device, WGPUQueue queue, CWebGPUSwapchain* swapchain)
{
	m_device = device;
	m_queue = queue;
	m_swapchain = swapchain;
}
#endif

void CWebGPUCommandContext::BeginFrame()
{
#if JBRO_PLATFORM_WEB
	ReleaseFrameObjects();
	if (m_device)
	{
		WGPUCommandEncoderDescriptor desc = {};
		m_commandEncoder = wgpuDeviceCreateCommandEncoder(m_device, &desc);
	}
#endif
}

void CWebGPUCommandContext::BeginRenderPass(const RenderPassDesc& desc)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_commandEncoder || nullptr == m_swapchain)
	{
		return;
	}

	WGPUTextureView renderTargetView = nullptr;
	if (desc.ColorAttachment.Target)
	{
		CWebGPUTexture* targetTexture = static_cast<CWebGPUTexture*>(desc.ColorAttachment.Target.TryGet());
		renderTargetView = targetTexture ? targetTexture->GetTextureView() : nullptr;
	}
	else
	{
		renderTargetView = m_swapchain->AcquireCurrentTextureView();
	}

	if (nullptr == renderTargetView)
	{
		return;
	}

	WGPURenderPassColorAttachment colorAttachment = {};
	colorAttachment.view = renderTargetView;
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	colorAttachment.loadOp = ERHILoadOp::Clear == desc.ColorAttachment.LoadOp ? WGPULoadOp_Clear : WGPULoadOp_Load;
	colorAttachment.storeOp = ERHIStoreOp::Store == desc.ColorAttachment.StoreOp ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
	colorAttachment.clearValue.r = desc.ColorAttachment.ClearColor.R;
	colorAttachment.clearValue.g = desc.ColorAttachment.ClearColor.G;
	colorAttachment.clearValue.b = desc.ColorAttachment.ClearColor.B;
	colorAttachment.clearValue.a = desc.ColorAttachment.ClearColor.A;

	WGPURenderPassDescriptor renderPassDesc = {};
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &colorAttachment;
	m_renderPass = wgpuCommandEncoderBeginRenderPass(m_commandEncoder, &renderPassDesc);
#else
	(void)desc;
#endif
}

void CWebGPUCommandContext::EndRenderPass()
{
#if JBRO_PLATFORM_WEB
	if (m_renderPass)
	{
		wgpuRenderPassEncoderEnd(m_renderPass);
		wgpuRenderPassEncoderRelease(m_renderPass);
		m_renderPass = nullptr;
	}
#endif
}

void CWebGPUCommandContext::EndFrame()
{
#if JBRO_PLATFORM_WEB
	if (m_commandEncoder)
	{
		WGPUCommandBufferDescriptor desc = {};
		m_pendingCommandBuffer = wgpuCommandEncoderFinish(m_commandEncoder, &desc);
		wgpuCommandEncoderRelease(m_commandEncoder);
		m_commandEncoder = nullptr;
	}

	if (m_queue && m_pendingCommandBuffer)
	{
		wgpuQueueSubmit(m_queue, 1, &m_pendingCommandBuffer);
		wgpuCommandBufferRelease(m_pendingCommandBuffer);
		m_pendingCommandBuffer = nullptr;
	}
#endif
}

void CWebGPUCommandContext::SetGraphicsPipeline(SafePtr<IRHIGraphicsPipeline> pipeline)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_renderPass || false == pipeline.IsValid())
	{
		return;
	}

	m_currentPipeline = static_cast<CWebGPUGraphicsPipeline*>(pipeline.TryGet());
	if (nullptr == m_currentPipeline || nullptr == m_currentPipeline->GetRenderPipeline())
	{
		return;
	}

	wgpuRenderPassEncoderSetPipeline(m_renderPass, m_currentPipeline->GetRenderPipeline());
#else
	(void)pipeline;
#endif
}

void CWebGPUCommandContext::SetVertexBuffer(std::uint32_t slot, SafePtr<IRHIBuffer> buffer, std::uint32_t, std::uint32_t offset)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_renderPass || false == buffer.IsValid())
	{
		return;
	}

	CWebGPUBuffer* webBuffer = static_cast<CWebGPUBuffer*>(buffer.TryGet());
	if (webBuffer && webBuffer->GetNativeBuffer())
	{
		wgpuRenderPassEncoderSetVertexBuffer(m_renderPass, slot, webBuffer->GetNativeBuffer(), offset, webBuffer->GetDesc().SizeInBytes - offset);
	}
#else
	(void)slot;
	(void)buffer;
	(void)offset;
#endif
}

void CWebGPUCommandContext::SetIndexBuffer(SafePtr<IRHIBuffer> buffer)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_renderPass || false == buffer.IsValid())
	{
		return;
	}

	CWebGPUBuffer* webBuffer = static_cast<CWebGPUBuffer*>(buffer.TryGet());
	if (webBuffer && webBuffer->GetNativeBuffer())
	{
		wgpuRenderPassEncoderSetIndexBuffer(m_renderPass, webBuffer->GetNativeBuffer(), WGPUIndexFormat_Uint32, 0, webBuffer->GetDesc().SizeInBytes);
	}
#else
	(void)buffer;
#endif
}

void CWebGPUCommandContext::SetConstantBuffer(ERHIProgramStage, std::uint32_t, SafePtr<IRHIBuffer> buffer)
{
#if JBRO_PLATFORM_WEB
	m_constantBuffer = buffer;
#else
	(void)buffer;
#endif
}

void CWebGPUCommandContext::SetTexture(ERHIProgramStage, std::uint32_t, SafePtr<IRHITexture> texture)
{
#if JBRO_PLATFORM_WEB
	m_texture = texture;
#else
	(void)texture;
#endif
}

void CWebGPUCommandContext::SetSampler(ERHIProgramStage, std::uint32_t, SafePtr<IRHISampler> sampler)
{
#if JBRO_PLATFORM_WEB
	m_sampler = sampler;
#else
	(void)sampler;
#endif
}

void CWebGPUCommandContext::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_renderPass)
	{
		return;
	}
	wgpuRenderPassEncoderSetViewport(
		m_renderPass,
		x,
		y,
		width > 0.0f ? width : 1.0f,
		height > 0.0f ? height : 1.0f,
		minDepth,
		maxDepth);
#else
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)minDepth;
	(void)maxDepth;
#endif
}

void CWebGPUCommandContext::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex)
{
#if JBRO_PLATFORM_WEB
	if (m_renderPass)
	{
		WGPUBindGroup bindGroup = CreateCurrentBindGroup();
		if (bindGroup)
		{
			wgpuRenderPassEncoderSetBindGroup(m_renderPass, 0, bindGroup, 0, nullptr);
		}
		wgpuRenderPassEncoderDraw(m_renderPass, vertexCount, 1, firstVertex, 0);
		if (bindGroup)
		{
			wgpuBindGroupRelease(bindGroup);
		}
	}
#else
	(void)vertexCount;
	(void)firstVertex;
#endif
}

void CWebGPUCommandContext::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::uint32_t baseVertex)
{
#if JBRO_PLATFORM_WEB
	if (m_renderPass)
	{
		WGPUBindGroup bindGroup = CreateCurrentBindGroup();
		if (bindGroup)
		{
			wgpuRenderPassEncoderSetBindGroup(m_renderPass, 0, bindGroup, 0, nullptr);
		}
		wgpuRenderPassEncoderDrawIndexed(m_renderPass, indexCount, 1, firstIndex, baseVertex, 0);
		if (bindGroup)
		{
			wgpuBindGroupRelease(bindGroup);
		}
	}
#else
	(void)indexCount;
	(void)firstIndex;
	(void)baseVertex;
#endif
}

#if JBRO_PLATFORM_WEB
void CWebGPUCommandContext::ReleaseFrameObjects()
{
	if (m_renderPass)
	{
		wgpuRenderPassEncoderRelease(m_renderPass);
		m_renderPass = nullptr;
	}
	if (m_commandEncoder)
	{
		wgpuCommandEncoderRelease(m_commandEncoder);
		m_commandEncoder = nullptr;
	}
	if (m_pendingCommandBuffer)
	{
		wgpuCommandBufferRelease(m_pendingCommandBuffer);
		m_pendingCommandBuffer = nullptr;
	}
}

WGPUBindGroup CWebGPUCommandContext::CreateCurrentBindGroup()
{
	if (nullptr == m_device || nullptr == m_currentPipeline || false == m_constantBuffer.IsValid() || false == m_texture.IsValid() || false == m_sampler.IsValid())
	{
		return nullptr;
	}

	CWebGPUBuffer* constantBuffer = static_cast<CWebGPUBuffer*>(m_constantBuffer.TryGet());
	CWebGPUTexture* texture = static_cast<CWebGPUTexture*>(m_texture.TryGet());
	CWebGPUSampler* sampler = static_cast<CWebGPUSampler*>(m_sampler.TryGet());
	if (nullptr == constantBuffer || nullptr == texture || nullptr == sampler)
	{
		return nullptr;
	}

	WGPUBindGroupEntry entries[3] = {};
	entries[0].binding = 0;
	entries[0].buffer = constantBuffer->GetNativeBuffer();
	entries[0].offset = 0;
	entries[0].size = constantBuffer->GetDesc().SizeInBytes;
	entries[1].binding = 1;
	entries[1].textureView = texture->GetTextureView();
	entries[2].binding = 2;
	entries[2].sampler = sampler->GetNativeSampler();

	WGPUBindGroupDescriptor desc = {};
	desc.layout = m_currentPipeline->GetBindGroupLayout();
	desc.entryCount = 3;
	desc.entries = entries;
	return wgpuDeviceCreateBindGroup(m_device, &desc);
}
#endif

