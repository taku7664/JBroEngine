#include "D3D12Device.h"

#include <limits>

namespace JBro::Internal
{
    namespace
    {
        D3D12_RENDER_PASS_BEGINNING_ACCESS BuildBeginningAccess(
            const ColorAttachmentDesc& attachment,
            DXGI_FORMAT format)
        {
            D3D12_RENDER_PASS_BEGINNING_ACCESS access = {};
            switch (attachment.loadOperation)
            {
            case LoadOperation::Load:
                access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                break;
            case LoadOperation::Clear:
                access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
                access.Clear.ClearValue.Format = format;
                access.Clear.ClearValue.Color[0] = attachment.clearColor.red;
                access.Clear.ClearValue.Color[1] = attachment.clearColor.green;
                access.Clear.ClearValue.Color[2] = attachment.clearColor.blue;
                access.Clear.ClearValue.Color[3] = attachment.clearColor.alpha;
                break;
            case LoadOperation::Discard:
                access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
                break;
            }
            return access;
        }

        D3D12_RENDER_PASS_ENDING_ACCESS BuildEndingAccess(StoreOperation operation)
        {
            D3D12_RENDER_PASS_ENDING_ACCESS access = {};
            access.Type = operation == StoreOperation::Store
                ? D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE
                : D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
            return access;
        }

        bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }
    }

    void D3D12CommandContext::Initialize(
        D3D12Device& device,
        ID3D12GraphicsCommandList& commandList,
        ID3D12GraphicsCommandList4* commandList4,
        bool nativeRenderPasses)
    {
        m_device = &device;
        m_commandList = &commandList;
        m_commandList4 = commandList4;
        m_nativeRenderPasses = nativeRenderPasses && commandList4 != nullptr;
        m_discardAtEndCount = 0;
        m_activePushConstantCount = 0;
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    void D3D12CommandContext::Reset()
    {
        m_discardAtEndCount = 0;
        m_activePushConstantCount = 0;
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    bool D3D12CommandContext::IsRenderPassActive() const
    {
        return m_renderPassActive;
    }

    bool D3D12CommandContext::BeginRenderPass(const RenderPassDesc& desc)
    {
        constexpr std::uint32_t MaxColorAttachments = 8;

        if (m_device == nullptr
            || m_commandList == nullptr
            || m_renderPassActive
            || desc.colorAttachments.data == nullptr
            || desc.colorAttachments.size == 0
            || desc.colorAttachments.size > MaxColorAttachments
            || desc.depthStencilAttachment != nullptr)
        {
            return false;
        }

        D3D12RenderTargetBinding bindings[MaxColorAttachments] = {};
        D3D12_RESOURCE_BARRIER barriers[MaxColorAttachments] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE descriptors[MaxColorAttachments] = {};
        std::uint32_t barrierCount = 0;

        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
        {
            if (false == m_device->ResolveRenderTarget(desc.colorAttachments.data[index].texture, bindings[index]))
            {
                return false;
            }
            descriptors[index] = bindings[index].descriptor;
        }

        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
        {
            if (*bindings[index].state != D3D12_RESOURCE_STATE_RENDER_TARGET)
            {
                D3D12_RESOURCE_BARRIER& barrier = barriers[barrierCount++];
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = bindings[index].resource;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = *bindings[index].state;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                *bindings[index].state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
        }

        if (barrierCount != 0)
        {
            m_commandList->ResourceBarrier(barrierCount, barriers);
        }

        if (m_nativeRenderPasses)
        {
            D3D12_RENDER_PASS_RENDER_TARGET_DESC renderTargets[MaxColorAttachments] = {};
            for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
            {
                renderTargets[index].cpuDescriptor = descriptors[index];
                renderTargets[index].BeginningAccess = BuildBeginningAccess(
                    desc.colorAttachments.data[index],
                    bindings[index].format);
                renderTargets[index].EndingAccess = BuildEndingAccess(
                    desc.colorAttachments.data[index].storeOperation);
            }

            m_commandList4->BeginRenderPass(
                desc.colorAttachments.size,
                renderTargets,
                nullptr,
                D3D12_RENDER_PASS_FLAG_NONE);
        }
        else
        {
            m_commandList->OMSetRenderTargets(
                desc.colorAttachments.size,
                descriptors,
                FALSE,
                nullptr);

            for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
            {
                const ColorAttachmentDesc& attachment = desc.colorAttachments.data[index];
                if (attachment.loadOperation == LoadOperation::Clear)
                {
                    const float color[4] = {
                        attachment.clearColor.red,
                        attachment.clearColor.green,
                        attachment.clearColor.blue,
                        attachment.clearColor.alpha};
                    m_commandList->ClearRenderTargetView(descriptors[index], color, 0, nullptr);
                }
                else if (attachment.loadOperation == LoadOperation::Discard)
                {
                    m_commandList->DiscardResource(bindings[index].resource, nullptr);
                }

                if (attachment.storeOperation == StoreOperation::Discard)
                {
                    m_discardAtEnd[m_discardAtEndCount++] = bindings[index].resource;
                }
            }
        }

        m_renderPassActive = true;
        return true;
    }

    void D3D12CommandContext::EndRenderPass()
    {
        if (false == m_renderPassActive)
        {
            return;
        }

        if (m_nativeRenderPasses)
        {
            m_commandList4->EndRenderPass();
        }
        else
        {
            for (std::uint32_t index = 0; index < m_discardAtEndCount; ++index)
            {
                m_commandList->DiscardResource(m_discardAtEnd[index], nullptr);
            }
        }

        m_discardAtEndCount = 0;
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    void D3D12CommandContext::SetViewport(const Viewport& viewport)
    {
        if (m_commandList == nullptr || false == m_renderPassActive)
        {
            return;
        }

        D3D12_VIEWPORT nativeViewport = {};
        nativeViewport.TopLeftX = viewport.x;
        nativeViewport.TopLeftY = viewport.y;
        nativeViewport.Width = viewport.width;
        nativeViewport.Height = viewport.height;
        nativeViewport.MinDepth = viewport.minDepth;
        nativeViewport.MaxDepth = viewport.maxDepth;
        m_commandList->RSSetViewports(1, &nativeViewport);
    }

    void D3D12CommandContext::SetScissor(const ScissorRect& scissor)
    {
        if (m_commandList == nullptr || false == m_renderPassActive)
        {
            return;
        }

        const D3D12_RECT nativeScissor = {
            scissor.left,
            scissor.top,
            scissor.right,
            scissor.bottom};
        m_commandList->RSSetScissorRects(1, &nativeScissor);
    }

    bool D3D12CommandContext::SetGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (false == m_renderPassActive || m_device == nullptr || m_commandList == nullptr)
        {
            return false;
        }

        D3D12PipelineBinding binding;
        if (false == m_device->ResolveGraphicsPipeline(pipeline, binding))
        {
            return false;
        }

        m_commandList->SetGraphicsRootSignature(binding.rootSignature);
        m_commandList->SetPipelineState(binding.pipeline);
        m_commandList->IASetPrimitiveTopology(binding.topology);
        m_activePushConstantCount = binding.pushConstantCount;
        m_pipelineActive = true;
        return true;
    }

    bool D3D12CommandContext::SetVertexBuffer(
        std::uint32_t slot,
        BufferHandle buffer,
        std::uint32_t stride,
        std::size_t offset)
    {
        D3D12BufferBinding binding;
        if (false == m_renderPassActive
            || false == m_pipelineActive
            || m_device == nullptr
            || stride == 0
            || false == m_device->ResolveBuffer(buffer, binding)
            || false == HasBufferUsage(binding.usage, BufferUsage::Vertex)
            || offset >= binding.size)
        {
            return false;
        }

        const std::uint64_t remainingSize = binding.size - offset;
        const std::uint32_t viewSize = remainingSize > (std::numeric_limits<std::uint32_t>::max)()
            ? (std::numeric_limits<std::uint32_t>::max)()
            : static_cast<std::uint32_t>(remainingSize);
        D3D12_VERTEX_BUFFER_VIEW view = {};
        view.BufferLocation = binding.gpuAddress + offset;
        view.SizeInBytes = viewSize;
        view.StrideInBytes = stride;
        m_commandList->IASetVertexBuffers(slot, 1, &view);
        return true;
    }

    bool D3D12CommandContext::SetIndexBuffer(
        BufferHandle buffer,
        IndexFormat format,
        std::size_t offset)
    {
        D3D12BufferBinding binding;
        if (false == m_renderPassActive
            || false == m_pipelineActive
            || m_device == nullptr
            || false == m_device->ResolveBuffer(buffer, binding)
            || false == HasBufferUsage(binding.usage, BufferUsage::Index)
            || offset >= binding.size)
        {
            return false;
        }

        const std::uint64_t remainingSize = binding.size - offset;
        const std::uint32_t viewSize = remainingSize > (std::numeric_limits<std::uint32_t>::max)()
            ? (std::numeric_limits<std::uint32_t>::max)()
            : static_cast<std::uint32_t>(remainingSize);
        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = binding.gpuAddress + offset;
        view.SizeInBytes = viewSize;
        view.Format = format == IndexFormat::UInt16
            ? DXGI_FORMAT_R16_UINT
            : DXGI_FORMAT_R32_UINT;
        m_commandList->IASetIndexBuffer(&view);
        return true;
    }

    bool D3D12CommandContext::SetGraphicsConstants(JArrayView<std::byte> data)
    {
        if (false == m_renderPassActive
            || false == m_pipelineActive
            || data.size != m_activePushConstantCount * sizeof(std::uint32_t)
            || (data.size != 0 && data.data == nullptr))
        {
            return false;
        }

        if (data.size != 0)
        {
            m_commandList->SetGraphicsRoot32BitConstants(
                0,
                m_activePushConstantCount,
                data.data,
                0);
        }
        return true;
    }

    bool D3D12CommandContext::DrawIndexedInstanced(
        std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t baseVertex,
        std::uint32_t firstInstance)
    {
        if (false == m_renderPassActive
            || false == m_pipelineActive
            || indexCount == 0
            || instanceCount == 0)
        {
            return false;
        }

        m_commandList->DrawIndexedInstanced(
            indexCount,
            instanceCount,
            firstIndex,
            baseVertex,
            firstInstance);
        return true;
    }
}
