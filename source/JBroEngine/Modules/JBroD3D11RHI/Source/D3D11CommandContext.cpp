#include "D3D11Device.h"

#include <cstring>

namespace JBro::Internal
{
    namespace
    {
        bool HasStage(ShaderStage stages, ShaderStage stage)
        {
            return (static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(stage)) != 0;
        }

        bool HasBufferUsage(BufferUsage usages, BufferUsage usage)
        {
            return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(usage)) != 0;
        }
    }

    void D3D11CommandContext::Bind(D3D11Device* device, ID3D11DeviceContext* context)
    {
        m_device = device;
        m_context = context;
        Reset();
    }

    void D3D11CommandContext::Reset()
    {
        m_activePipeline = {};
        m_activePushConstantBytes = 0;
        m_activeConstantBuffer = nullptr;
        m_renderPassActive = false;
        m_pipelineActive = false;
    }

    bool D3D11CommandContext::BeginRenderPass(const RenderPassDesc& desc)
    {
        if (m_device == nullptr || m_context == nullptr || m_renderPassActive
            || desc.colorAttachments.data == nullptr || desc.colorAttachments.size == 0
            || desc.colorAttachments.size > MaxColorAttachments)
        {
            return false;
        }
        ID3D11RenderTargetView* views[MaxColorAttachments] = {};
        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
        {
            if (false == m_device->ResolveRenderTargetView(desc.colorAttachments.data[index].texture, views[index]))
            {
                return false;
            }
        }
        ID3D11DepthStencilView* depthView = nullptr;
        if (desc.depthStencilAttachment != nullptr
            && false == m_device->ResolveDepthStencilView(desc.depthStencilAttachment->texture, depthView))
        {
            return false;
        }
        // 그리려는 텍스처가 아직 셰이더 자원으로 걸려 있으면 D3D11 이 경고를 내며 스스로 뗀다.
        // 먼저 떼어 조용히 간다 - 에디터가 게임 뷰를 그린 뒤 같은 프레임에 읽는 길이 그렇다.
        ID3D11ShaderResourceView* noResources[MaxBoundTextures] = {};
        m_context->PSSetShaderResources(0, MaxBoundTextures, noResources);
        m_context->OMSetRenderTargets(desc.colorAttachments.size, views, depthView);
        for (std::uint32_t index = 0; index < desc.colorAttachments.size; ++index)
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
        m_renderPassActive = true;
        m_pipelineActive = false;
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

    bool D3D11CommandContext::SetGraphicsPipeline(GraphicsPipelineHandle pipeline)
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
        m_activeConstantBuffer = state->constantBuffer.Get();
        m_pipelineActive = true;
        return true;
    }

    bool D3D11CommandContext::SetVertexBuffer(
        std::uint32_t slot,
        BufferHandle buffer,
        std::uint32_t stride,
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
        m_context->IASetVertexBuffers(slot, 1, &native, &stride, &nativeOffset);
        return true;
    }

    bool D3D11CommandContext::SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset)
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

    bool D3D11CommandContext::SetGraphicsConstants(JArrayView<std::byte> data)
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
        // 상수 버퍼는 16 바이트 단위라 짧은 자료를 채운 사본으로 통째로 쓴다.
        unsigned char padded[256] = {};
        std::memcpy(padded, data.data, data.size);
        m_context->UpdateSubresource(m_activeConstantBuffer, 0, nullptr, padded, 0, 0);
        if (HasStage(m_activePushConstantStages, ShaderStage::Vertex))
        {
            m_context->VSSetConstantBuffers(0, 1, &m_activeConstantBuffer);
        }
        if (HasStage(m_activePushConstantStages, ShaderStage::Pixel))
        {
            m_context->PSSetConstantBuffers(0, 1, &m_activeConstantBuffer);
        }
        return true;
    }

    bool D3D11CommandContext::SetTexture(std::uint32_t slot, TextureHandle texture)
    {
        ID3D11ShaderResourceView* view = nullptr;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= MaxBoundTextures || false == m_device->ResolveShaderResourceView(texture, view))
        {
            return false;
        }
        m_context->PSSetShaderResources(slot, 1, &view);
        return true;
    }

    bool D3D11CommandContext::SetSampler(std::uint32_t slot, SamplerHandle sampler)
    {
        ID3D11SamplerState* native = nullptr;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= MaxBoundSamplers || false == m_device->ResolveSampler(sampler, native))
        {
            return false;
        }
        m_context->PSSetSamplers(slot, 1, &native);
        return true;
    }

    bool D3D11CommandContext::DrawIndexedInstanced(
        std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t baseVertex,
        std::uint32_t firstInstance)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || indexCount == 0 || instanceCount == 0)
        {
            return false;
        }
        m_context->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        return true;
    }
}
