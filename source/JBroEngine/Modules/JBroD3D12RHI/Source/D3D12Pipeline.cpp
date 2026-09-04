#include "D3D12Device.h"

#include <limits>

namespace JBro::Internal
{
    namespace
    {
        constexpr std::uint32_t MaxVertexBuffers = 8;
        constexpr std::uint32_t MaxVertexAttributes = 16;
        constexpr std::uint32_t MaxColorTargets = 8;

        std::uint32_t NextGeneration(std::uint32_t generation)
        {
            ++generation;
            if (generation == 0)
            {
                generation = 1;
            }
            return generation;
        }

        DXGI_FORMAT ToNativeFormat(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::RGBA8Unorm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RGBA8UnormSrgb:
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case TextureFormat::BGRA8Unorm:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case TextureFormat::BGRA8UnormSrgb:
                return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case TextureFormat::RGBA16Float:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case TextureFormat::D32Float:
                return DXGI_FORMAT_D32_FLOAT;
            case TextureFormat::Unknown:
                return DXGI_FORMAT_UNKNOWN;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        DXGI_FORMAT ToNativeVertexFormat(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case VertexFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case VertexFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        std::uint32_t VertexFormatSize(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float2:
                return 8;
            case VertexFormat::Float3:
                return 12;
            case VertexFormat::Float4:
                return 16;
            }
            return 0;
        }

        D3D12_SHADER_VISIBILITY ToNativeVisibility(ShaderStage stages)
        {
            if (stages == ShaderStage::Vertex)
            {
                return D3D12_SHADER_VISIBILITY_VERTEX;
            }
            if (stages == ShaderStage::Pixel)
            {
                return D3D12_SHADER_VISIBILITY_PIXEL;
            }
            return D3D12_SHADER_VISIBILITY_ALL;
        }

        bool BuildInputLayout(
            const GraphicsPipelineDesc& desc,
            D3D12_INPUT_ELEMENT_DESC* elements,
            std::uint32_t& elementCount)
        {
            if (desc.vertexBuffers.size > MaxVertexBuffers
                || (desc.vertexBuffers.size != 0 && desc.vertexBuffers.data == nullptr))
            {
                return false;
            }

            elementCount = 0;
            for (std::uint32_t bufferIndex = 0; bufferIndex < desc.vertexBuffers.size; ++bufferIndex)
            {
                const VertexBufferLayoutDesc& layout = desc.vertexBuffers.data[bufferIndex];
                if (layout.stride == 0
                    || layout.attributes.size == 0
                    || layout.attributes.data == nullptr
                    || elementCount + layout.attributes.size > MaxVertexAttributes)
                {
                    return false;
                }

                for (std::uint32_t attributeIndex = 0;
                    attributeIndex < layout.attributes.size;
                    ++attributeIndex)
                {
                    const VertexAttributeDesc& attribute = layout.attributes.data[attributeIndex];
                    const std::uint32_t formatSize = VertexFormatSize(attribute.format);
                    if (formatSize == 0
                        || attribute.offset > layout.stride
                        || formatSize > layout.stride - attribute.offset)
                    {
                        return false;
                    }

                    D3D12_INPUT_ELEMENT_DESC& nativeAttribute = elements[elementCount++];
                    nativeAttribute.SemanticName = "ATTRIBUTE";
                    nativeAttribute.SemanticIndex = attribute.shaderLocation;
                    nativeAttribute.Format = ToNativeVertexFormat(attribute.format);
                    nativeAttribute.InputSlot = bufferIndex;
                    nativeAttribute.AlignedByteOffset = attribute.offset;
                    nativeAttribute.InputSlotClass = layout.stepMode == VertexStepMode::Instance
                        ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                        : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    nativeAttribute.InstanceDataStepRate = layout.stepMode == VertexStepMode::Instance
                        ? 1
                        : 0;
                }
            }
            return true;
        }
    }

    GraphicsPipelineHandle D3D12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
    {
        if (m_status != FrameStatus::Ready
            || m_device == nullptr
            || m_frameActive
            || desc.vertexShader.data == nullptr
            || desc.vertexShader.size == 0
            || desc.pixelShader.data == nullptr
            || desc.pixelShader.size == 0
            || desc.colorFormats.data == nullptr
            || desc.colorFormats.size == 0
            || desc.colorFormats.size > MaxColorTargets
            || desc.pushConstantBytes > 256
            || (desc.pushConstantBytes % sizeof(std::uint32_t)) != 0
            || (desc.pushConstantBytes != 0
                && static_cast<std::uint8_t>(desc.pushConstantStages) == 0)
            || (desc.depthFormat != TextureFormat::Unknown
                && desc.depthFormat != TextureFormat::D32Float))
        {
            return {};
        }

        D3D12_INPUT_ELEMENT_DESC inputElements[MaxVertexAttributes] = {};
        std::uint32_t inputElementCount = 0;
        if (false == BuildInputLayout(desc, inputElements, inputElementCount))
        {
            return {};
        }

        for (std::uint32_t index = 0; index < desc.colorFormats.size; ++index)
        {
            if (ToNativeFormat(desc.colorFormats.data[index]) == DXGI_FORMAT_UNKNOWN
                || desc.colorFormats.data[index] == TextureFormat::D32Float)
            {
                return {};
            }
        }

        CollectRetiredResources();
        std::uint32_t slotIndex = MaxGraphicsPipelines;
        for (std::uint32_t index = 0; index < MaxGraphicsPipelines; ++index)
        {
            if (false == m_graphicsPipelines[index].occupied
                && m_graphicsPipelines[index].pipeline == nullptr)
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == MaxGraphicsPipelines)
        {
            return {};
        }

        D3D12_ROOT_PARAMETER rootParameter = {};
        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        if (desc.pushConstantBytes != 0)
        {
            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rootParameter.Constants.ShaderRegister = 0;
            rootParameter.Constants.RegisterSpace = 0;
            rootParameter.Constants.Num32BitValues =
                desc.pushConstantBytes / sizeof(std::uint32_t);
            rootParameter.ShaderVisibility = ToNativeVisibility(desc.pushConstantStages);
            rootDesc.NumParameters = 1;
            rootDesc.pParameters = &rootParameter;
        }

        ComPtr<ID3DBlob> serializedRootSignature;
        ComPtr<ID3DBlob> rootSignatureErrors;
        if (FAILED(D3D12SerializeRootSignature(
            &rootDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSignature,
            &rootSignatureErrors)))
        {
            return {};
        }

        ComPtr<ID3D12RootSignature> rootSignature;
        if (FAILED(m_device->CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature))))
        {
            return {};
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = {};
        pipelineDesc.pRootSignature = rootSignature.Get();
        pipelineDesc.VS = {desc.vertexShader.data, desc.vertexShader.size};
        pipelineDesc.PS = {desc.pixelShader.data, desc.pixelShader.size};
        pipelineDesc.BlendState.AlphaToCoverageEnable = FALSE;
        pipelineDesc.BlendState.IndependentBlendEnable = FALSE;
        for (std::uint32_t index = 0; index < desc.colorFormats.size; ++index)
        {
            D3D12_RENDER_TARGET_BLEND_DESC& target = pipelineDesc.BlendState.RenderTarget[index];
            target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            if (desc.blend == BlendMode::Alpha)
            {
                target.BlendEnable = TRUE;
                target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOp = D3D12_BLEND_OP_ADD;
                target.SrcBlendAlpha = D3D12_BLEND_ONE;
                target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
        }
        pipelineDesc.SampleMask = (std::numeric_limits<UINT>::max)();
        pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipelineDesc.RasterizerState.CullMode = desc.cull == CullMode::None
            ? D3D12_CULL_MODE_NONE
            : desc.cull == CullMode::Front
                ? D3D12_CULL_MODE_FRONT
                : D3D12_CULL_MODE_BACK;
        pipelineDesc.RasterizerState.FrontCounterClockwise = FALSE;
        pipelineDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        pipelineDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        pipelineDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
        pipelineDesc.RasterizerState.MultisampleEnable = FALSE;
        pipelineDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        pipelineDesc.RasterizerState.ForcedSampleCount = 0;
        pipelineDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        pipelineDesc.DepthStencilState.DepthEnable = desc.depthFormat != TextureFormat::Unknown;
        pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        pipelineDesc.DepthStencilState.StencilEnable = FALSE;
        pipelineDesc.InputLayout = {inputElements, inputElementCount};
        pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDesc.NumRenderTargets = desc.colorFormats.size;
        for (std::uint32_t index = 0; index < desc.colorFormats.size; ++index)
        {
            pipelineDesc.RTVFormats[index] = ToNativeFormat(desc.colorFormats.data[index]);
        }
        pipelineDesc.DSVFormat = ToNativeFormat(desc.depthFormat);
        pipelineDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pipeline;
        if (FAILED(m_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline))))
        {
            return {};
        }

        D3D12PipelineState& state = m_graphicsPipelines[slotIndex];
        state.rootSignature = rootSignature;
        state.pipeline = pipeline;
        state.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        state.pushConstantCount = desc.pushConstantBytes / sizeof(std::uint32_t);
        state.retirementFence = 0;
        state.occupied = true;
        return {slotIndex, state.generation};
    }

    void D3D12Device::DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (pipeline.index >= MaxGraphicsPipelines)
        {
            return;
        }

        D3D12PipelineState& state = m_graphicsPipelines[pipeline.index];
        if (false == state.occupied || state.generation != pipeline.generation)
        {
            return;
        }

        state.occupied = false;
        state.generation = NextGeneration(state.generation);
        state.retirementFence = m_frameActive
            ? PendingRetirementFence
            : m_lastSubmittedFenceValue;
        if (m_frameActive)
        {
            m_hasPendingRetirementFence = true;
        }
    }

    bool D3D12Device::ResolveGraphicsPipeline(
        GraphicsPipelineHandle pipeline,
        D3D12PipelineBinding& binding)
    {
        if (pipeline.index >= MaxGraphicsPipelines)
        {
            return false;
        }

        D3D12PipelineState& state = m_graphicsPipelines[pipeline.index];
        if (false == state.occupied || state.generation != pipeline.generation)
        {
            return false;
        }

        binding.pipeline = state.pipeline.Get();
        binding.rootSignature = state.rootSignature.Get();
        binding.topology = state.topology;
        binding.pushConstantCount = state.pushConstantCount;
        return true;
    }
}
