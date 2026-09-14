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

        if (desc.sampledTextureCount > MaxBoundTextures || desc.samplerCount > MaxBoundSamplers)
        {
            // 한 드로우가 묶을 수 있는 수를 넘었다. 루트 시그니처는 만들어지고 나면
            // 바꿀 수 없으므로, 여기서 거절하지 않으면 그리는 자리에서 조용히 잘린다.
            return {};
        }

        // 파라미터 자리는 상수 → 텍스처 표 → 샘플러 표 순서다. 없는 것은 자리를 차지하지 않으므로
        // 그리는 쪽이 번호를 짐작할 수 없고, 그래서 그 번호를 파이프라인에 적어 둔다.
        D3D12_ROOT_PARAMETER rootParameters[3] = {};
        std::uint32_t parameterCount = 0;
        std::uint32_t textureTableParameter = InvalidRootParameter;
        std::uint32_t samplerTableParameter = InvalidRootParameter;

        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        if (desc.pushConstantBytes != 0)
        {
            D3D12_ROOT_PARAMETER& parameter = rootParameters[parameterCount];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.Constants.ShaderRegister = 0;
            parameter.Constants.RegisterSpace = 0;
            parameter.Constants.Num32BitValues =
                desc.pushConstantBytes / sizeof(std::uint32_t);
            parameter.ShaderVisibility = ToNativeVisibility(desc.pushConstantStages);
            ++parameterCount;
        }

        D3D12_DESCRIPTOR_RANGE textureRange = {};
        if (desc.sampledTextureCount != 0)
        {
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = desc.sampledTextureCount;
            textureRange.BaseShaderRegister = 0;
            textureRange.RegisterSpace = 0;
            textureRange.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER& parameter = rootParameters[parameterCount];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.DescriptorTable.NumDescriptorRanges = 1;
            parameter.DescriptorTable.pDescriptorRanges = &textureRange;
            // 텍스처는 픽셀 셰이더만 읽는다. 정점 단계에 열어 두면 드라이버가
            // 필요 없는 가시성까지 준비한다.
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            textureTableParameter = parameterCount;
            ++parameterCount;
        }

        D3D12_DESCRIPTOR_RANGE samplerRange = {};
        if (desc.samplerCount != 0)
        {
            samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            samplerRange.NumDescriptors = desc.samplerCount;
            samplerRange.BaseShaderRegister = 0;
            samplerRange.RegisterSpace = 0;
            samplerRange.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER& parameter = rootParameters[parameterCount];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.DescriptorTable.NumDescriptorRanges = 1;
            parameter.DescriptorTable.pDescriptorRanges = &samplerRange;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            samplerTableParameter = parameterCount;
            ++parameterCount;
        }

        if (parameterCount != 0)
        {
            rootDesc.NumParameters = parameterCount;
            rootDesc.pParameters = rootParameters;
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
        state.sampledTextureCount = desc.sampledTextureCount;
        state.samplerCount = desc.samplerCount;
        state.textureTableParameter = textureTableParameter;
        state.samplerTableParameter = samplerTableParameter;
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
        binding.sampledTextureCount = state.sampledTextureCount;
        binding.samplerCount = state.samplerCount;
        binding.textureTableParameter = state.textureTableParameter;
        binding.samplerTableParameter = state.samplerTableParameter;
        return true;
    }
}
