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

    Bool D3D11CommandContext::BeginRenderPass(const RenderPassDesc& desc)
    {
        if (m_device == nullptr || m_context == nullptr || m_renderPassActive
            || desc.colorAttachments.data == nullptr || desc.colorAttachments.size == 0
            || desc.colorAttachments.size > MaxColorAttachments)
        {
            return false;
        }
        ID3D11RenderTargetView* views[MaxColorAttachments] = {};
        for (UInt32 index = 0; index < desc.colorAttachments.size; ++index)
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
        // 먼저 떼어 조용히 간다 - 에디터가 시뮬레이션 뷰를 그린 뒤 같은 프레임에 읽는 길이 그렇다.
        ID3D11ShaderResourceView* noResources[MaxBoundTextures] = {};
        m_context->PSSetShaderResources(0, MaxBoundTextures, noResources);
        m_context->OMSetRenderTargets(desc.colorAttachments.size, views, depthView);
        // 래스터라이저가 시저를 켜 두므로 기본 시저가 있어야 한다 - 없으면 D3D11 의 초기 시저는 빈 사각형이라
        // 뷰포트만 준 호출자는 아무것도 그리지 못한다. 첫 첨부 전체로 둔다.
        {
            ComPtr<ID3D11Resource> resource;
            views[0]->GetResource(&resource);
            ComPtr<ID3D11Texture2D> texture;
            if (resource != nullptr && SUCCEEDED(resource.As(&texture)))
            {
                D3D11_TEXTURE2D_DESC textureDesc = {};
                texture->GetDesc(&textureDesc);
                const D3D11_RECT full = {0, 0, static_cast<LONG>(textureDesc.Width), static_cast<LONG>(textureDesc.Height)};
                m_context->RSSetScissorRects(1, &full);
                D3D11_VIEWPORT viewport = {};
                viewport.Width = static_cast<JBro::Float>(textureDesc.Width);
                viewport.Height = static_cast<JBro::Float>(textureDesc.Height);
                viewport.MaxDepth = 1.0f;
                m_context->RSSetViewports(1, &viewport);
            }
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
        m_renderPassActive = true;
        m_pipelineActive = false;
        return true;
    }

    Bool D3D11CommandContext::CopyTexture(TextureHandle source, TextureHandle destination)
    {
        ID3D11RenderTargetView* fromView = nullptr;
        ID3D11RenderTargetView* toView = nullptr;
        if (m_device == nullptr || m_context == nullptr || m_renderPassActive
            || false == m_device->ResolveRenderTargetView(source, fromView) || false == m_device->ResolveRenderTargetView(destination, toView))
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
        if (fromDesc.Width != toDesc.Width || fromDesc.Height != toDesc.Height || fromDesc.Format != toDesc.Format)
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
        m_activeConstantBuffer = state->constantBuffer;
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
        ID3D11ShaderResourceView* view = nullptr;
        if (false == m_renderPassActive || false == m_pipelineActive || m_device == nullptr
            || slot >= MaxBoundTextures || false == m_device->ResolveShaderResourceView(texture, view))
        {
            return false;
        }
        m_context->PSSetShaderResources(slot, 1, &view);
        return true;
    }

    Bool D3D11CommandContext::SetSampler(UInt32 slot, SamplerHandle sampler)
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

    Bool D3D11CommandContext::DrawIndexedInstanced(
        UInt32 indexCount,
        UInt32 instanceCount,
        UInt32 firstIndex,
        Int32 baseVertex,
        UInt32 firstInstance)
    {
        if (false == m_renderPassActive || false == m_pipelineActive || indexCount == 0 || instanceCount == 0)
        {
            return false;
        }
        m_context->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        return true;
    }
}
