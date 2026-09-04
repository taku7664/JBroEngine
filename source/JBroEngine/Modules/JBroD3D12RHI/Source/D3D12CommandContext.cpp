#include "D3D12Device.h"

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
        m_renderPassActive = false;
    }

    void D3D12CommandContext::Reset()
    {
        m_discardAtEndCount = 0;
        m_renderPassActive = false;
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
}
