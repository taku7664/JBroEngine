#include "D3D11Device.h"

namespace JBro::Internal
{
    namespace
    {
        DXGI_FORMAT ToVertexFormat(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case VertexFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case VertexFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case VertexFormat::UByte4Norm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case VertexFormat::UShort4Norm:
                return DXGI_FORMAT_R16G16B16A16_UNORM;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        D3D11_CULL_MODE ToCullMode(CullMode mode)
        {
            switch (mode)
            {
            case CullMode::None:
                return D3D11_CULL_NONE;
            case CullMode::Front:
                return D3D11_CULL_FRONT;
            case CullMode::Back:
                return D3D11_CULL_BACK;
            }
            return D3D11_CULL_BACK;
        }

        constexpr std::uint32_t MaxInputElements = 32;
    }

    GraphicsPipelineHandle D3D11Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive
            || desc.vertexShader.data == nullptr || desc.vertexShader.size == 0
            || desc.pixelShader.data == nullptr || desc.pixelShader.size == 0
            || desc.vertexBuffers.size > MaxVertexSlots
            || desc.colorFormats.data == nullptr || desc.colorFormats.size == 0
            || desc.colorFormats.size > MaxColorAttachments
            || (desc.depthFormat != TextureFormat::Unknown && desc.depthFormat != TextureFormat::D32Float)
            || desc.topology != PrimitiveTopology::TriangleList
            || desc.pushConstantBytes % 4 != 0 || desc.pushConstantBytes > 256
            || desc.sampledTextureCount > MaxBoundTextures || desc.samplerCount > MaxBoundSamplers)
        {
            return {};
        }
        std::uint32_t index = MaxGraphicsPipelines;
        for (std::uint32_t at = 0; at < MaxGraphicsPipelines; ++at)
        {
            if (false == m_graphicsPipelines[at].occupied)
            {
                index = at;
                break;
            }
        }
        if (index == MaxGraphicsPipelines)
        {
            return {};
        }

        D3D11PipelineState pipeline;
        // DXBC 여야 한다. DXIL 을 주면 여기서 실패한다 - D3D11 은 SM 6 을 모른다.
        if (FAILED(m_device->CreateVertexShader(desc.vertexShader.data, desc.vertexShader.size, nullptr,
                &pipeline.vertexShader))
            || FAILED(m_device->CreatePixelShader(desc.pixelShader.data, desc.pixelShader.size, nullptr,
                &pipeline.pixelShader)))
        {
            return {};
        }

        // 정점 배치. 의미소는 `ATTRIBUTEn` 이고 n 이 `shaderLocation` 이다 - 셰이더가 그렇게 적혀 있다.
        D3D11_INPUT_ELEMENT_DESC elements[MaxInputElements] = {};
        std::uint32_t elementCount = 0;
        for (std::uint32_t slot = 0; slot < desc.vertexBuffers.size; ++slot)
        {
            const VertexBufferLayoutDesc& layout = desc.vertexBuffers.data[slot];
            for (std::uint32_t at = 0; at < layout.attributes.size; ++at)
            {
                if (elementCount >= MaxInputElements)
                {
                    return {};
                }
                const VertexAttributeDesc& attribute = layout.attributes.data[at];
                D3D11_INPUT_ELEMENT_DESC& element = elements[elementCount++];
                element.SemanticName = "ATTRIBUTE";
                element.SemanticIndex = attribute.shaderLocation;
                element.Format = ToVertexFormat(attribute.format);
                element.InputSlot = slot;
                element.AlignedByteOffset = attribute.offset;
                element.InputSlotClass = layout.stepMode == VertexStepMode::Instance
                    ? D3D11_INPUT_PER_INSTANCE_DATA
                    : D3D11_INPUT_PER_VERTEX_DATA;
                element.InstanceDataStepRate = layout.stepMode == VertexStepMode::Instance ? 1 : 0;
            }
        }
        if (elementCount != 0
            && FAILED(m_device->CreateInputLayout(elements, elementCount, desc.vertexShader.data,
                desc.vertexShader.size, &pipeline.inputLayout)))
        {
            return {};
        }

        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (desc.blend == BlendMode::Alpha)
        {
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        for (std::uint32_t at = 1; at < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++at)
        {
            blend.RenderTarget[at] = blend.RenderTarget[0];
        }

        D3D11_RASTERIZER_DESC rasterizer = {};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = ToCullMode(desc.cull);
        // D3D12 백엔드와 같다: 화면에서 시계 방향이 앞면이다.
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthClipEnable = TRUE;
        rasterizer.ScissorEnable = TRUE;

        D3D11_DEPTH_STENCIL_DESC depth = {};
        depth.DepthEnable = desc.depthFormat != TextureFormat::Unknown && desc.depthTest;
        depth.DepthWriteMask = desc.depthFormat != TextureFormat::Unknown && desc.depthWrite
            ? D3D11_DEPTH_WRITE_MASK_ALL
            : D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        depth.StencilEnable = FALSE;

        if (FAILED(m_device->CreateBlendState(&blend, &pipeline.blendState))
            || FAILED(m_device->CreateRasterizerState(&rasterizer, &pipeline.rasterizerState))
            || FAILED(m_device->CreateDepthStencilState(&depth, &pipeline.depthStencilState)))
        {
            return {};
        }

        if (desc.pushConstantBytes != 0)
        {
            D3D11_BUFFER_DESC constants = {};
            constants.ByteWidth = (desc.pushConstantBytes + 15) & ~15u;
            // 드로우마다 바뀌는 자료다. DYNAMIC + Map(DISCARD) 이 이 용도의 빠른 길이고, DEFAULT 에
            // `UpdateSubresource` 는 드라이버가 사본을 뜨는 느린 길이다.
            constants.Usage = D3D11_USAGE_DYNAMIC;
            constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(m_device->CreateBuffer(&constants, nullptr, &pipeline.constantBuffer)))
            {
                return {};
            }
        }
        pipeline.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        pipeline.pushConstantBytes = desc.pushConstantBytes;
        pipeline.pushConstantStages = desc.pushConstantStages;
        pipeline.generation = m_graphicsPipelines[index].generation;
        pipeline.occupied = true;
        m_graphicsPipelines[index] = std::move(pipeline);
        return GraphicsPipelineHandle{index, m_graphicsPipelines[index].generation};
    }

    void D3D11Device::DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (pipeline.index >= MaxGraphicsPipelines)
        {
            return;
        }
        D3D11PipelineState& state = m_graphicsPipelines[pipeline.index];
        if (false == state.occupied || state.generation != pipeline.generation)
        {
            return;
        }
        const std::uint32_t generation = state.generation + 1;
        state = {};
        state.generation = generation;
    }

    D3D11PipelineState* D3D11Device::ResolvePipeline(GraphicsPipelineHandle pipeline)
    {
        if (pipeline.index >= MaxGraphicsPipelines)
        {
            return nullptr;
        }
        D3D11PipelineState& state = m_graphicsPipelines[pipeline.index];
        if (false == state.occupied || state.generation != pipeline.generation)
        {
            return nullptr;
        }
        return &state;
    }
}
