#include "VulkanDevice.h"

#include <JBro/Types/Array.h>

#include <cstring>

namespace JBro::Internal
{
    namespace
    {
        constexpr std::uint32_t MaxVertexAttributes = 16;
        // 스펙이 보장하는 푸시 상수 최소치다. 계약은 256 까지 말하지만 여기서는 그 안에서만 받는다.
        constexpr std::uint32_t MaxPushConstantBytes = 128;

        VkFormat ToNativeVertexFormat(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float2:
                return VK_FORMAT_R32G32_SFLOAT;
            case VertexFormat::Float3:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexFormat::Float4:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case VertexFormat::UByte4Norm:
                return VK_FORMAT_R8G8B8A8_UNORM;
            }
            return VK_FORMAT_UNDEFINED;
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
            case VertexFormat::UByte4Norm:
                return 4;
            }
            return 0;
        }

        VkShaderStageFlags ToNativeStages(ShaderStage stages)
        {
            VkShaderStageFlags flags = 0;
            if ((static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(ShaderStage::Vertex)) != 0)
            {
                flags |= VK_SHADER_STAGE_VERTEX_BIT;
            }
            if ((static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(ShaderStage::Pixel)) != 0)
            {
                flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            return flags;
        }

        bool BuildVertexInput(const GraphicsPipelineDesc& desc, VkVertexInputBindingDescription* bindings,
            VkVertexInputAttributeDescription* attributes, std::uint32_t& attributeCount)
        {
            if (desc.vertexBuffers.size > MaxVertexSlots
                || (desc.vertexBuffers.size != 0 && desc.vertexBuffers.data == nullptr))
            {
                return false;
            }
            attributeCount = 0;
            for (std::uint32_t bufferIndex = 0; bufferIndex < desc.vertexBuffers.size; ++bufferIndex)
            {
                const VertexBufferLayoutDesc& layout = desc.vertexBuffers.data[bufferIndex];
                if (layout.stride == 0 || layout.attributes.size == 0 || layout.attributes.data == nullptr
                    || attributeCount + layout.attributes.size > MaxVertexAttributes)
                {
                    return false;
                }
                bindings[bufferIndex].binding = bufferIndex;
                bindings[bufferIndex].stride = layout.stride;
                bindings[bufferIndex].inputRate = layout.stepMode == VertexStepMode::Instance
                    ? VK_VERTEX_INPUT_RATE_INSTANCE
                    : VK_VERTEX_INPUT_RATE_VERTEX;
                for (std::uint32_t attributeIndex = 0; attributeIndex < layout.attributes.size; ++attributeIndex)
                {
                    const VertexAttributeDesc& attribute = layout.attributes.data[attributeIndex];
                    const std::uint32_t formatSize = VertexFormatSize(attribute.format);
                    if (formatSize == 0 || attribute.offset > layout.stride
                        || formatSize > layout.stride - attribute.offset)
                    {
                        return false;
                    }
                    VkVertexInputAttributeDescription& native = attributes[attributeCount++];
                    // `ATTRIBUTEn` 의미소는 SPIR-V 에서 location n 이 된다(dxc 가 그렇게 굽는다).
                    native.location = attribute.shaderLocation;
                    native.binding = bufferIndex;
                    native.format = ToNativeVertexFormat(attribute.format);
                    native.offset = attribute.offset;
                }
            }
            return true;
        }

        bool CreateShaderModule(VkDevice device, const ShaderBytecode& bytecode, VkShaderModule& module)
        {
            module = VK_NULL_HANDLE;
            // SPIR-V 는 4 바이트 단어의 열이고 첫 단어가 매직이다. DXIL·DXBC 를 잘못 넘긴 것을 여기서 잡는다.
            if (bytecode.data == nullptr || bytecode.size < 20 || (bytecode.size % 4) != 0)
            {
                return false;
            }
            std::uint32_t magic = 0;
            std::memcpy(&magic, bytecode.data, sizeof(magic));
            if (magic != 0x07230203u)
            {
                return false;
            }
            // 헤더의 바이트 배열은 정렬을 약속하지 않는다. 단어 배열로 옮겨서 넘긴다.
            Array<std::uint32_t> words;
            words.Resize(bytecode.size / 4);
            std::memcpy(words.Data(), bytecode.data, bytecode.size);
            VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = bytecode.size;
            info.pCode = words.Data();
            if (vk.vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
            {
                module = VK_NULL_HANDLE;
                return false;
            }
            return true;
        }
    }

    GraphicsPipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive
            || desc.vertexShader.data == nullptr || desc.vertexShader.size == 0
            || desc.pixelShader.data == nullptr || desc.pixelShader.size == 0
            || desc.colorFormats.data == nullptr || desc.colorFormats.size == 0
            || desc.colorFormats.size > MaxColorAttachments
            || desc.pushConstantBytes > MaxPushConstantBytes
            || (desc.pushConstantBytes % sizeof(std::uint32_t)) != 0
            || (desc.pushConstantBytes != 0 && ToNativeStages(desc.pushConstantStages) == 0)
            || (desc.depthFormat != TextureFormat::Unknown && desc.depthFormat != TextureFormat::D32Float)
            || desc.topology != PrimitiveTopology::TriangleList
            || desc.sampledTextureCount > MaxBoundTextures || desc.samplerCount > MaxBoundSamplers)
        {
            return {};
        }
        VkFormat colorFormats[MaxColorAttachments] = {};
        for (std::uint32_t index = 0; index < desc.colorFormats.size; ++index)
        {
            colorFormats[index] = ToVulkanFormat(desc.colorFormats.data[index]);
            if (colorFormats[index] == VK_FORMAT_UNDEFINED || desc.colorFormats.data[index] == TextureFormat::D32Float)
            {
                return {};
            }
        }
        VkVertexInputBindingDescription bindings[MaxVertexSlots] = {};
        VkVertexInputAttributeDescription attributes[MaxVertexAttributes] = {};
        std::uint32_t attributeCount = 0;
        if (false == BuildVertexInput(desc, bindings, attributes, attributeCount))
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

        VulkanPipelineState& state = m_graphicsPipelines[index];
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule pixelModule = VK_NULL_HANDLE;
        auto fail = [&]()
        {
            if (vertexModule != VK_NULL_HANDLE)
            {
                vk.vkDestroyShaderModule(m_device, vertexModule, nullptr);
            }
            if (pixelModule != VK_NULL_HANDLE)
            {
                vk.vkDestroyShaderModule(m_device, pixelModule, nullptr);
            }
            if (state.pipeline != VK_NULL_HANDLE)
            {
                vk.vkDestroyPipeline(m_device, state.pipeline, nullptr);
            }
            if (state.layout != VK_NULL_HANDLE)
            {
                vk.vkDestroyPipelineLayout(m_device, state.layout, nullptr);
            }
            if (state.setLayout != VK_NULL_HANDLE)
            {
                vk.vkDestroyDescriptorSetLayout(m_device, state.setLayout, nullptr);
            }
            const std::uint32_t generation = state.generation;
            state = {};
            state.generation = generation;
            return GraphicsPipelineHandle{};
        };
        if (false == CreateShaderModule(m_device, desc.vertexShader, vertexModule)
            || false == CreateShaderModule(m_device, desc.pixelShader, pixelModule))
        {
            return fail();
        }

        // set 0: 텍스처는 binding 8+, 샘플러는 16+. 둘 다 없으면 set 자체가 없다.
        VkDescriptorSetLayoutBinding setBindings[MaxBoundTextures + MaxBoundSamplers] = {};
        std::uint32_t setBindingCount = 0;
        for (std::uint32_t slot = 0; slot < desc.sampledTextureCount; ++slot)
        {
            VkDescriptorSetLayoutBinding& binding = setBindings[setBindingCount++];
            binding.binding = TextureBindingBase + slot;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        for (std::uint32_t slot = 0; slot < desc.samplerCount; ++slot)
        {
            VkDescriptorSetLayoutBinding& binding = setBindings[setBindingCount++];
            binding.binding = SamplerBindingBase + slot;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (setBindingCount != 0)
        {
            VkDescriptorSetLayoutCreateInfo setLayout = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            setLayout.bindingCount = setBindingCount;
            setLayout.pBindings = setBindings;
            if (vk.vkCreateDescriptorSetLayout(m_device, &setLayout, nullptr, &state.setLayout) != VK_SUCCESS)
            {
                state.setLayout = VK_NULL_HANDLE;
                return fail();
            }
        }
        VkPushConstantRange pushRange = {};
        pushRange.stageFlags = ToNativeStages(desc.pushConstantStages);
        pushRange.size = desc.pushConstantBytes;
        VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = state.setLayout != VK_NULL_HANDLE ? 1 : 0;
        layout.pSetLayouts = &state.setLayout;
        layout.pushConstantRangeCount = desc.pushConstantBytes != 0 ? 1 : 0;
        layout.pPushConstantRanges = &pushRange;
        if (vk.vkCreatePipelineLayout(m_device, &layout, nullptr, &state.layout) != VK_SUCCESS)
        {
            state.layout = VK_NULL_HANDLE;
            return fail();
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "VSMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = pixelModule;
        stages[1].pName = "PSMain";

        VkPipelineVertexInputStateCreateInfo vertexInput = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = desc.vertexBuffers.size;
        vertexInput.pVertexBindingDescriptions = bindings;
        vertexInput.vertexAttributeDescriptionCount = attributeCount;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = desc.cull == CullMode::None ? VK_CULL_MODE_NONE
            : desc.cull == CullMode::Front           ? VK_CULL_MODE_FRONT_BIT
                                                     : VK_CULL_MODE_BACK_BIT;
        // 뷰포트를 뒤집어(높이 음수) D3D 와 같은 화면 좌표를 쓴다. 그러면 화면에서 시계 방향인 것이 앞면이다 -
        // D3D 의 FrontCounterClockwise=FALSE 와 같은 뜻이다.
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = desc.depthFormat != TextureFormat::Unknown ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = depth.depthTestEnable;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depth.maxDepthBounds = 1.0f;

        VkPipelineColorBlendAttachmentState blendTargets[MaxColorAttachments] = {};
        for (std::uint32_t at = 0; at < desc.colorFormats.size; ++at)
        {
            VkPipelineColorBlendAttachmentState& target = blendTargets[at];
            target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT;
            if (desc.blend == BlendMode::Alpha)
            {
                target.blendEnable = VK_TRUE;
                target.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                target.colorBlendOp = VK_BLEND_OP_ADD;
                target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                target.alphaBlendOp = VK_BLEND_OP_ADD;
            }
        }
        VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = desc.colorFormats.size;
        blend.pAttachments = blendTargets;

        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        // 렌더 패스 객체 대신 첨부 포맷만 말한다(동적 렌더링).
        VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = desc.colorFormats.size;
        rendering.pColorAttachmentFormats = colorFormats;
        rendering.depthAttachmentFormat = ToVulkanFormat(desc.depthFormat);

        VkGraphicsPipelineCreateInfo pipeline = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.pNext = &rendering;
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &vertexInput;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &multisample;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = state.layout;
        if (vk.vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &state.pipeline) != VK_SUCCESS)
        {
            state.pipeline = VK_NULL_HANDLE;
            return fail();
        }
        // 모듈은 파이프라인에 구워졌다. 바로 지운다.
        vk.vkDestroyShaderModule(m_device, vertexModule, nullptr);
        vk.vkDestroyShaderModule(m_device, pixelModule, nullptr);
        state.pushConstantStages = pushRange.stageFlags;
        state.pushConstantBytes = desc.pushConstantBytes;
        state.sampledTextureCount = desc.sampledTextureCount;
        state.samplerCount = desc.samplerCount;
        state.occupied = true;
        return GraphicsPipelineHandle{index, state.generation};
    }

    void VulkanDevice::DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline)
    {
        if (false == pipeline.IsValid() || pipeline.index >= MaxGraphicsPipelines)
        {
            return;
        }
        VulkanPipelineState& state = m_graphicsPipelines[pipeline.index];
        if (false == state.occupied || state.generation != pipeline.generation)
        {
            return;
        }
        Retire(VulkanRetiredObject::Kind::Pipeline, reinterpret_cast<std::uint64_t>(state.pipeline));
        Retire(VulkanRetiredObject::Kind::PipelineLayout, reinterpret_cast<std::uint64_t>(state.layout));
        Retire(VulkanRetiredObject::Kind::DescriptorSetLayout, reinterpret_cast<std::uint64_t>(state.setLayout));
        const std::uint32_t generation = VulkanNextGeneration(state.generation);
        state = {};
        state.generation = generation;
    }

    VulkanPipelineState* VulkanDevice::ResolvePipeline(GraphicsPipelineHandle pipeline)
    {
        if (false == pipeline.IsValid() || pipeline.index >= MaxGraphicsPipelines)
        {
            return nullptr;
        }
        VulkanPipelineState& state = m_graphicsPipelines[pipeline.index];
        if (false == state.occupied || state.generation != pipeline.generation)
        {
            return nullptr;
        }
        return &state;
    }
}
