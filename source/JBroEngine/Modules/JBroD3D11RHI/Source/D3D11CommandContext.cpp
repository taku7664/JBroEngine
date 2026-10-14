#include "D3D11Device.h"

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Internal
{
    namespace
    {
        Bool HasStage(ShaderStage stages, ShaderStage stage)
        {
            return (static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(stage)) != 0;
        }

        Bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<JBro::UInt32>(usages) & static_cast<JBro::UInt32>(usage)) != 0;
        }
    }

    void D3D11CommandContext::Bind(D3D11Device* device, ID3D11DeviceContext* context)
    {
        m_device = device;
        m_context = context;
        m_context1.Reset();
        if (context != nullptr)
        {
            context->QueryInterface(IID_PPV_ARGS(&m_context1));
        }
        Reset();
    }

    void D3D11CommandContext::Reset()
    {
        m_activePipeline = {};
        m_activePushConstantBytes = 0;
        m_activeBindings = {};
        m_activeConstantBuffer = nullptr;
        for (Bool& bound : m_constantBufferBound)
        {
            bound = false;
        }
        m_passTargets.Clear();
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    Bool D3D11CommandContext::BeginRenderPass(const RenderPassDesc& desc)
    {
        if (m_device == nullptr || m_context == nullptr || m_renderPassActive
            || desc.colorAttachments.data == nullptr || desc.colorAttachments.size == 0
            || desc.colorAttachments.size > MaxColorAttachments)
        {
            return false;
        }
        ID3D11RenderTargetView* views[MaxColorAttachments] = {};
        Extent2D firstExtent;
        for (UInt32 index = 0; index < desc.colorAttachments.size; ++index)
        {
            const ColorAttachmentDesc& attachment = desc.colorAttachments.data[index];
            Extent2D extent;
            if (false == m_device->ResolveRenderTargetView(attachment.texture, attachment.mipLevel, views[index], extent))
            {
                return false;
            }
            if (index == 0)
            {
                firstExtent = extent;
            }
        }
        ID3D11DepthStencilView* depthView = nullptr;
        if (desc.depthStencilAttachment != nullptr
            && false == m_device->ResolveDepthStencilView(desc.depthStencilAttachment->texture, depthView))
        {
            return false;
        }
        // 그리려는 텍스처가 아직 셰이더 자원으로 걸려 있으면 D3D11 이 경고를 내며 스스로 뗀다.
        // 먼저 떼어 조용히 간다 - 에디터가 시뮬레이션 뷰를 그린 뒤 같은 프레임에 읽는 길이 그렇다.
        ID3D11ShaderResourceView* noResources[MaxBoundTextures] = {};
        m_context->VSSetShaderResources(0, MaxBoundTextures, noResources);
        m_context->PSSetShaderResources(0, MaxBoundTextures, noResources);
        m_context->OMSetRenderTargets(desc.colorAttachments.size, views, depthView);
        // 래스터라이저가 시저를 켜 두므로 기본 시저가 있어야 한다 - 없으면 D3D11 의 초기 시저는 빈 사각형이라
        // 뷰포트만 준 호출자는 아무것도 그리지 못한다. 첫 첨부 전체로 둔다 - 그 mip 의 크기다.
        {
            const D3D11_RECT full = {0, 0, static_cast<LONG>(firstExtent.width.Get()),
                static_cast<LONG>(firstExtent.height.Get())};
            m_context->RSSetScissorRects(1, &full);
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<JBro::Float>(firstExtent.width);
            viewport.Height = static_cast<JBro::Float>(firstExtent.height);
            viewport.MaxDepth = 1.0f;
            m_context->RSSetViewports(1, &viewport);
        }
        for (UInt32 index = 0; index < desc.colorAttachments.size; ++index)
        {
            const ColorAttachmentDesc& attachment = desc.colorAttachments.data[index];
            if (attachment.loadOperation == LoadOperation::Clear)
            {
                const float color[4] = {attachment.clearColor.red, attachment.clearColor.green,
                    attachment.clearColor.blue, attachment.clearColor.alpha};
                m_context->ClearRenderTargetView(views[index], color);
            }
        }
        if (depthView != nullptr && desc.depthStencilAttachment->depthLoadOperation == LoadOperation::Clear)
        {
            m_context->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH,
                desc.depthStencilAttachment->clearDepth, desc.depthStencilAttachment->clearStencil);
        }
        m_passTargets.Clear();
        for (UInt32 index = 0; index < desc.colorAttachments.size; ++index)
        {
            m_passTargets.Add(desc.colorAttachments.data[index].texture, desc.colorAttachments.data[index].mipLevel);
        }
        if (desc.depthStencilAttachment != nullptr)
        {
            m_passTargets.Add(desc.depthStencilAttachment->texture, 0);
        }
        m_renderPassActive = true;
        m_pipelineActive = false;
        return true;
    }

    Bool D3D11CommandContext::CopyTexture(TextureHandle source, TextureHandle destination)
    {
        ID3D11RenderTargetView* fromView = nullptr;
        ID3D11RenderTargetView* toView = nullptr;
        Extent2D fromExtent;
        Extent2D toExtent;
        if (m_device == nullptr || m_context == nullptr || m_renderPassActive
            || false == m_device->ResolveRenderTargetView(source, 0, fromView, fromExtent)
            || false == m_device->ResolveRenderTargetView(destination, 0, toView, toExtent))
        {
            return false;
        }
        ComPtr<ID3D11Resource> from;
        ComPtr<ID3D11Resource> to;
        fromView->GetResource(&from);
        toView->GetResource(&to);
        ComPtr<ID3D11Texture2D> fromTexture;
        ComPtr<ID3D11Texture2D> toTexture;
        if (from == nullptr || to == nullptr || from.Get() == to.Get()
            || FAILED(from.As(&fromTexture)) || FAILED(to.As(&toTexture)))
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC fromDesc = {};
        D3D11_TEXTURE2D_DESC toDesc = {};
        fromTexture->GetDesc(&fromDesc);
        toTexture->GetDesc(&toDesc);
        // 통째 복사라 mip 이 하나인 것만 받는다(D3D12 와 같은 계약).
        if (fromDesc.Width != toDesc.Width || fromDesc.Height != toDesc.Height || fromDesc.Format != toDesc.Format
            || fromDesc.MipLevels != 1 || toDesc.MipLevels != 1)
        {
            return false;
        }
        m_context->CopyResource(to.Get(), from.Get());
        return true;
    }

    void D3D11CommandContext::EndRenderPass()
    {
        if (false == m_renderPassActive || m_context == nullptr)
        {
            return;
        }
        // 렌더 타깃을 떼어 두어야 다음에 그것을 셰이더가 읽을 수 있다.
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_passTargets.Clear();
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    void D3D11CommandContext::SetViewport(const Viewport& viewport)
    {
        if (m_context == nullptr)
        {
            return;
        }
        D3D11_VIEWPORT native = {};
        native.TopLeftX = viewport.x;
        native.TopLeftY = viewport.y;
        native.Width = viewport.width;
        native.Height = viewport.height;
        native.MinDepth = viewport.minDepth;
        native.MaxDepth = viewport.maxDepth;
        m_context->RSSetViewports(1, &native);
    }

    void D3D11CommandContext::SetScissor(const ScissorRect& scissor)
    {
        if (m_context == nullptr)
        {
            return;
        }
        D3D11_RECT native = {scissor.left, scissor.top, scissor.right, scissor.bottom};
        m_context->RSSetScissorRects(1, &native);
    }

    Bool D3D11CommandContext::SetGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (false == m_renderPassActive || m_device == nullptr)
        {
            return false;
        }
        D3D11PipelineState* state = m_device->ResolvePipeline(pipeline);
        if (state == nullptr)
        {
            return false;
        }
        m_context->IASetInputLayout(state->inputLayout.Get());
        m_context->IASetPrimitiveTopology(state->topology);
        m_context->VSSetShader(state->vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(state->pixelShader.Get(), nullptr, 0);
        const float blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_context->OMSetBlendState(state->blendState.Get(), blendFactor, 0xFFFFFFFFu);
        m_context->RSSetState(state->rasterizerState.Get());
        m_context->OMSetDepthStencilState(state->depthStencilState.Get(), 0);
        m_activePipeline = pipeline;
        m_activePushConstantBytes = state->pushConstantBytes;
        m_activePushConstantStages = state->pushConstantStages;
        m_activeBindings = state->bindings;
        m_activeConstantBuffer = state->constantBuffer;
        // 묶어 둔 상수 버퍼는 버린다 - D3D12 는 루트 시그니처가 바뀌면 루트 인자가 사라지고, 계약이 그쪽에 맞춘다.
        // 컨텍스트의 자리에는 남아 있으므로 빠뜨린 슬롯을 그리기가 알아채도록 표시만 지운다.
        for (Bool& bound : m_constantBufferBound)
        {
            bound = false;
        }
        m_pipelineActive = true;
        return true;
    }

    Bool D3D11CommandContext::SetVertexBuffer(
        UInt32 slot,
        BufferHandle buffer,
        UInt32 stride,
        std::size_t offset)
    {
        ID3D11Buffer* native = nullptr;
        BufferDesc desc;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= MaxVertexSlots || stride == 0
            || false == m_device->ResolveBuffer(buffer, native, desc)
            || false == HasBufferUsage(desc.usage, BufferUsage::Vertex)
            || offset >= desc.size)
        {
            return false;
        }
        const UINT nativeOffset = static_cast<UINT>(offset);
        const UINT nativeStride = stride;
        m_context->IASetVertexBuffers(slot, 1, &native, &nativeStride, &nativeOffset);
        return true;
    }

    Bool D3D11CommandContext::SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset)
    {
        ID3D11Buffer* native = nullptr;
        BufferDesc desc;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || false == m_device->ResolveBuffer(buffer, native, desc)
            || false == HasBufferUsage(desc.usage, BufferUsage::Index)
            || offset >= desc.size)
        {
            return false;
        }
        m_context->IASetIndexBuffer(native,
            format == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
            static_cast<UINT>(offset));
        return true;
    }

    Bool D3D11CommandContext::SetGraphicsConstants(JArrayView<std::byte> data)
    {
        if (false == m_renderPassActive || false == m_pipelineActive
            || data.size != m_activePushConstantBytes || (data.size != 0 && data.data == nullptr))
        {
            return false;
        }
        if (data.size == 0)
        {
            return true;
        }
        if (m_activeConstantBuffer == nullptr)
        {
            return false;
        }
        // 동적 버퍼를 통째로 갈아 쓴다(DISCARD). 16 단위 꼬리는 셰이더가 읽지 않으므로 채우지 않는다.
        ID3D11Buffer* constantBuffer = m_activeConstantBuffer.Get();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || mapped.pData == nullptr)
        {
            return false;
        }
        std::memcpy(mapped.pData, data.data, data.size);
        m_context->Unmap(constantBuffer, 0);
        if (HasStage(m_activePushConstantStages, ShaderStage::Vertex))
        {
            m_context->VSSetConstantBuffers(0, 1, &constantBuffer);
        }
        if (HasStage(m_activePushConstantStages, ShaderStage::Pixel))
        {
            m_context->PSSetConstantBuffers(0, 1, &constantBuffer);
        }
        return true;
    }

    Bool D3D11CommandContext::SetTexture(UInt32 slot, TextureHandle texture)
    {
        return BindTexture(slot, texture, D3D11Device::AllMips);
    }

    Bool D3D11CommandContext::SetTextureMip(UInt32 slot, TextureHandle texture, UInt32 mipLevel)
    {
        if (mipLevel == D3D11Device::AllMips)
        {
            return false;
        }
        return BindTexture(slot, texture, mipLevel);
    }

    Bool D3D11CommandContext::BindTexture(UInt32 slot, TextureHandle texture, UInt32 mipLevel)
    {
        // 이 패스가 그리는 자리를 읽으려 한다(`SetTextureMip` 의 계약).
        if (m_passTargets.Overlaps(texture, mipLevel, mipLevel == D3D11Device::AllMips))
        {
            return false;
        }
        ID3D11ShaderResourceView* view = nullptr;
        // 파이프라인이 선언하지 않은 자리는 거절한다 - D3D12·Vulkan 은 그 칸이 없어 거절하고, 계약이 같아야 한다.
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= m_activeBindings.sampledTextures.count
            || false == m_device->ResolveShaderResourceView(texture, mipLevel, view))
        {
            return false;
        }
        if (HasStage(m_activeBindings.sampledTextures.stages, ShaderStage::Vertex))
        {
            m_context->VSSetShaderResources(slot, 1, &view);
        }
        if (HasStage(m_activeBindings.sampledTextures.stages, ShaderStage::Pixel))
        {
            m_context->PSSetShaderResources(slot, 1, &view);
        }
        return true;
    }

    Bool D3D11CommandContext::SetSampler(UInt32 slot, SamplerHandle sampler)
    {
        ID3D11SamplerState* native = nullptr;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= m_activeBindings.samplers.count || false == m_device->ResolveSampler(sampler, native))
        {
            return false;
        }
        if (HasStage(m_activeBindings.samplers.stages, ShaderStage::Vertex))
        {
            m_context->VSSetSamplers(slot, 1, &native);
        }
        if (HasStage(m_activeBindings.samplers.stages, ShaderStage::Pixel))
        {
            m_context->PSSetSamplers(slot, 1, &native);
        }
        return true;
    }

    Bool D3D11CommandContext::SetConstantBuffer(UInt32 slot, BufferHandle buffer, std::size_t offset, std::size_t size)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot == 0 || slot > m_activeBindings.constantBuffers.count)
        {
            return false;
        }
        const DeviceCaps caps = m_device->GetCaps();
        ID3D11Buffer* native = nullptr;
        BufferDesc desc;
        if (false == m_device->ResolveBuffer(buffer, native, desc)
            || false == HasBufferUsage(desc.usage, BufferUsage::Constant)
            || size == 0
            || size > caps.maxConstantBufferBytes
            || offset % caps.constantBufferAlignment != 0
            || size % caps.constantBufferAlignment != 0
            || offset > desc.size
            || size > desc.size - offset
            || (offset != 0 && false == caps.constantBufferOffsets))
        {
            return false;
        }
        const ShaderStage stages = m_activeBindings.constantBuffers.stages;
        if (caps.constantBufferOffsets && m_context1 != nullptr)
        {
            // 단위는 16 B 상수다. 정렬 256 B 가 곧 상수 16 개라 11.1 의 "16 의 배수" 조건이 저절로 맞는다.
            const UINT firstConstant = static_cast<UINT>(offset / 16);
            const UINT constantCount = static_cast<UINT>(size / 16);
            if (HasStage(stages, ShaderStage::Vertex))
            {
                m_context1->VSSetConstantBuffers1(slot, 1, &native, &firstConstant, &constantCount);
            }
            if (HasStage(stages, ShaderStage::Pixel))
            {
                m_context1->PSSetConstantBuffers1(slot, 1, &native, &firstConstant, &constantCount);
            }
        }
        else
        {
            if (HasStage(stages, ShaderStage::Vertex))
            {
                m_context->VSSetConstantBuffers(slot, 1, &native);
            }
            if (HasStage(stages, ShaderStage::Pixel))
            {
                m_context->PSSetConstantBuffers(slot, 1, &native);
            }
        }
        m_constantBufferBound[slot] = true;
        return true;
    }

    Bool D3D11CommandContext::PrepareDraw() const
    {
        if (false == m_renderPassActive || false == m_pipelineActive)
        {
            return false;
        }
        // 선언한 상수 버퍼가 하나라도 비어 있으면 그리지 않는다. D3D11 은 지난 파이프라인이 남긴 버퍼를 조용히 읽는다.
        for (UInt32 slot = 1; slot <= m_activeBindings.constantBuffers.count; ++slot)
        {
            if (false == m_constantBufferBound[slot])
            {
                return false;
            }
        }
        return true;
    }

    Bool D3D11CommandContext::DrawIndexedInstanced(
        UInt32 indexCount,
        UInt32 instanceCount,
        UInt32 firstIndex,
        Int32 baseVertex,
        UInt32 firstInstance)
    {
        if (indexCount == 0 || instanceCount == 0 || false == PrepareDraw())
        {
            return false;
        }
        m_context->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        return true;
    }

    Bool D3D11CommandContext::DrawInstanced(
        UInt32 vertexCount,
        UInt32 instanceCount,
        UInt32 firstVertex,
        UInt32 firstInstance)
    {
        if (vertexCount == 0 || instanceCount == 0 || false == PrepareDraw())
        {
            return false;
        }
        m_context->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
        return true;
    }
}
