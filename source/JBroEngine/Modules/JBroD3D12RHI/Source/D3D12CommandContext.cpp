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

        // 깊이 첨부의 시작 접근. 색과 다른 점은 클리어 값이 깊이·스텐실이라는 것뿐이다.
        D3D12_RENDER_PASS_BEGINNING_ACCESS BuildDepthBeginningAccess(
            LoadOperation operation,
            float clearDepth,
            std::uint8_t clearStencil)
        {
            D3D12_RENDER_PASS_BEGINNING_ACCESS access = {};
            switch (operation)
            {
            case LoadOperation::Load:
                access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                break;
            case LoadOperation::Clear:
                access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
                access.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
                access.Clear.ClearValue.DepthStencil.Depth = clearDepth;
                access.Clear.ClearValue.DepthStencil.Stencil = clearStencil;
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
        m_activePipeline = {};
        for (std::uint32_t index = 0; index < MaxBoundTextures; ++index)
        {
            m_pendingTextures[index] = {};
        }
        for (std::uint32_t index = 0; index < MaxBoundSamplers; ++index)
        {
            m_pendingSamplers[index] = {};
        }
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    void D3D12CommandContext::Reset()
    {
        m_discardAtEndCount = 0;
        m_activePushConstantCount = 0;
        m_activePipeline = {};
        // 스테이징 자리는 프레임 슬롯의 것이다. 프레임이 바뀌면 지난 테이블은 남의 것이다.
        m_textureTableCount = 0;
        m_samplerTableCount = 0;
        m_textureTableCursor = 0;
        m_samplerTableCursor = 0;
        m_boundTextureTable = {};
        m_boundSamplerTable = {};
        for (std::uint32_t index = 0; index < MaxBoundTextures; ++index)
        {
            m_pendingTextures[index] = {};
        }
        for (std::uint32_t index = 0; index < MaxBoundSamplers; ++index)
        {
            m_pendingSamplers[index] = {};
        }
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    bool D3D12CommandContext::IsRenderPassActive() const
    {
        return m_renderPassActive;
    }

    bool D3D12CommandContext::BeginRenderPass(const RenderPassDesc& desc)
    {

        if (m_device == nullptr
            || m_commandList == nullptr
            || m_renderPassActive
            || desc.colorAttachments.data == nullptr
            || desc.colorAttachments.size == 0
            || desc.colorAttachments.size > MaxColorAttachments)
        {
            return false;
        }

        D3D12RenderTargetBinding bindings[MaxColorAttachments] = {};
        // 색 첨부들 + 깊이 하나. 깊이는 마지막 칸이다.
        D3D12_RESOURCE_BARRIER barriers[MaxColorAttachments + 1] = {};
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

        // 깊이 첨부(framework3d-plan §2.4). 3D 메시가 있는 뷰만 단다 - 스프라이트만 있는 프레임은
        // 전과 같은 길을 간다.
        const DepthStencilAttachmentDesc* depthDesc = desc.depthStencilAttachment;
        D3D12DepthStencilBinding depth;
        if (depthDesc != nullptr)
        {
            if (false == m_device->ResolveDepthStencil(depthDesc->texture, depth))
            {
                return false;
            }
            if (*depth.state != D3D12_RESOURCE_STATE_DEPTH_WRITE)
            {
                D3D12_RESOURCE_BARRIER& barrier = barriers[barrierCount++];
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = depth.resource;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = *depth.state;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                *depth.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            }
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

        // 샘플링도 되는 렌더 타깃은 패스가 끝나면 셰이더 읽기 상태로 되돌려야 한다.
        // 에디터가 게임 화면을 텍스처에 그려 놓고 같은 프레임에 그것을 읽는 길이 이것이다.
        m_sampledAtEndCount = 0;
        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
        {
            if (bindings[index].sampled)
            {
                m_sampledAtEnd[m_sampledAtEndCount++] = bindings[index];
            }
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

            D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depthStencil = {};
            if (depthDesc != nullptr)
            {
                depthStencil.cpuDescriptor = depth.descriptor;
                depthStencil.DepthBeginningAccess = BuildDepthBeginningAccess(
                    depthDesc->depthLoadOperation, depthDesc->clearDepth, depthDesc->clearStencil);
                // D32Float 에는 스텐실이 없다. 없는 면은 접근하지 않는다고 말한다.
                depthStencil.StencilBeginningAccess.Type =
                    D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
                depthStencil.DepthEndingAccess = BuildEndingAccess(depthDesc->depthStoreOperation);
                depthStencil.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
            }
            m_commandList4->BeginRenderPass(
                desc.colorAttachments.size,
                renderTargets,
                depthDesc != nullptr ? &depthStencil : nullptr,
                D3D12_RENDER_PASS_FLAG_NONE);
        }
        else
        {
            m_commandList->OMSetRenderTargets(
                desc.colorAttachments.size,
                descriptors,
                FALSE,
                depthDesc != nullptr ? &depth.descriptor : nullptr);
            if (depthDesc != nullptr && depthDesc->depthLoadOperation == LoadOperation::Clear)
            {
                m_commandList->ClearDepthStencilView(depth.descriptor, D3D12_CLEAR_FLAG_DEPTH,
                    depthDesc->clearDepth, depthDesc->clearStencil, 0, nullptr);
            }

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

        // **되돌리는 자리가 여기다.** 네이티브 렌더 패스 안에서는 배리어가 불법이므로
        // `SetTexture` 가 그때그때 바꿀 수 없고, 패스를 닫은 뒤에야 할 수 있다.
        if (m_sampledAtEndCount != 0)
        {
            D3D12_RESOURCE_BARRIER barriers[MaxColorAttachments] = {};
            std::uint32_t barrierCount = 0;
            for (std::uint32_t index = 0; index < m_sampledAtEndCount; ++index)
            {
                D3D12RenderTargetBinding& binding = m_sampledAtEnd[index];
                if (*binding.state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
                {
                    continue;
                }
                D3D12_RESOURCE_BARRIER& barrier = barriers[barrierCount++];
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = binding.resource;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = *binding.state;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                *binding.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            if (barrierCount != 0)
            {
                m_commandList->ResourceBarrier(barrierCount, barriers);
            }
            m_sampledAtEndCount = 0;
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
        m_activePipeline = binding;
        // 파이프라인이 바뀌면 묶어 둔 것도 버린다. 루트 시그니처가 달라졌으므로
        // 앞의 테이블 번호가 더 이상 같은 자리를 뜻하지 않는다. 걸려 있던 테이블도 새 루트에는 없는 것이다.
        m_boundTextureTable = {};
        m_boundSamplerTable = {};
        for (std::uint32_t index = 0; index < MaxBoundTextures; ++index)
        {
            m_pendingTextures[index] = {};
        }
        for (std::uint32_t index = 0; index < MaxBoundSamplers; ++index)
        {
            m_pendingSamplers[index] = {};
        }
        m_pipelineActive = true;
        return true;
    }

    bool D3D12CommandContext::SetTexture(std::uint32_t slot, TextureHandle texture)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr)
        {
            return false;
        }
        // 파이프라인이 선언하지 않은 자리다. 루트 시그니처에 그 칸이 없으므로
        // 받아 두어 봐야 그릴 때 아무 데도 가지 않는다.
        if (slot >= m_activePipeline.sampledTextureCount)
        {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        if (false == m_device->ResolveSampledTexture(texture, descriptor))
        {
            return false;
        }
        m_pendingTextures[slot] = descriptor;
        return true;
    }

    bool D3D12CommandContext::SetSampler(std::uint32_t slot, SamplerHandle sampler)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr)
        {
            return false;
        }
        if (slot >= m_activePipeline.samplerCount)
        {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
        if (false == m_device->ResolveSampler(sampler, descriptor))
        {
            return false;
        }
        m_pendingSamplers[slot] = descriptor;
        return true;
    }

    bool D3D12CommandContext::FindStagedTable(const StagedTable* tables, std::uint32_t tableCount,
        const D3D12_CPU_DESCRIPTOR_HANDLE* keys, std::uint32_t count, D3D12_GPU_DESCRIPTOR_HANDLE& table) const
    {
        for (std::uint32_t index = 0; index < tableCount; ++index)
        {
            const StagedTable& candidate = tables[index];
            if (candidate.count != count)
            {
                continue;
            }
            bool same = true;
            for (std::uint32_t slot = 0; slot < count && same; ++slot)
            {
                same = candidate.keys[slot].ptr == keys[slot].ptr;
            }
            if (same)
            {
                table = candidate.table;
                return true;
            }
        }
        return false;
    }

    void D3D12CommandContext::RememberStagedTable(StagedTable* tables, std::uint32_t& tableCount,
        std::uint32_t& cursor, const D3D12_CPU_DESCRIPTOR_HANDLE* keys, std::uint32_t count,
        D3D12_GPU_DESCRIPTOR_HANDLE table)
    {
        // 꽉 차면 가장 오래된 자리부터 돌려 쓴다.
        StagedTable& entry = tables[cursor];
        cursor = (cursor + 1) % CachedTables;
        tableCount = tableCount < CachedTables ? tableCount + 1 : CachedTables;
        entry.count = count;
        entry.table = table;
        for (std::uint32_t slot = 0; slot < MaxBoundTextures; ++slot)
        {
            entry.keys[slot] = slot < count ? keys[slot] : D3D12_CPU_DESCRIPTOR_HANDLE{};
        }
    }

    bool D3D12CommandContext::BindPendingDescriptors()
    {
        if (m_activePipeline.sampledTextureCount != 0)
        {
            // 선언한 자리가 하나라도 비어 있으면 그리지 않는다. 빈 칸을 그냥 두면
            // 셰이더가 남의 디스크립터를 읽고, 그것은 화면에 조용히 틀린 그림으로 나온다.
            for (std::uint32_t index = 0; index < m_activePipeline.sampledTextureCount; ++index)
            {
                if (m_pendingTextures[index].ptr == 0)
                {
                    return false;
                }
            }
            D3D12_GPU_DESCRIPTOR_HANDLE table = {};
            if (false == FindStagedTable(m_textureTables, m_textureTableCount, m_pendingTextures,
                    m_activePipeline.sampledTextureCount, table))
            {
                if (false == m_device->StageShaderResources(
                    m_pendingTextures, m_activePipeline.sampledTextureCount, table))
                {
                    return false;
                }
                RememberStagedTable(m_textureTables, m_textureTableCount, m_textureTableCursor, m_pendingTextures,
                    m_activePipeline.sampledTextureCount, table);
            }
            if (table.ptr != m_boundTextureTable.ptr)
            {
                m_commandList->SetGraphicsRootDescriptorTable(
                    m_activePipeline.textureTableParameter, table);
                m_boundTextureTable = table;
            }
        }

        if (m_activePipeline.samplerCount != 0)
        {
            for (std::uint32_t index = 0; index < m_activePipeline.samplerCount; ++index)
            {
                if (m_pendingSamplers[index].ptr == 0)
                {
                    return false;
                }
            }
            D3D12_GPU_DESCRIPTOR_HANDLE table = {};
            if (false == FindStagedTable(m_samplerTables, m_samplerTableCount, m_pendingSamplers,
                    m_activePipeline.samplerCount, table))
            {
                if (false == m_device->StageSamplers(
                    m_pendingSamplers, m_activePipeline.samplerCount, table))
                {
                    return false;
                }
                RememberStagedTable(m_samplerTables, m_samplerTableCount, m_samplerTableCursor, m_pendingSamplers,
                    m_activePipeline.samplerCount, table);
            }
            if (table.ptr != m_boundSamplerTable.ptr)
            {
                m_commandList->SetGraphicsRootDescriptorTable(
                    m_activePipeline.samplerTableParameter, table);
                m_boundSamplerTable = table;
            }
        }
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

        // 묶는 것은 여기서 한다. 슬롯이 다 모인 뒤라야 디스크립터를 연속으로 놓을 수 있고,
        // 테이블은 연속된 자리를 가리키기 때문이다.
        if (false == BindPendingDescriptors())
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
