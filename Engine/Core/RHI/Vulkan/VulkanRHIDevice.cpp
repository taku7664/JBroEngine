#include "pch.h"
#include "VulkanRHIDevice.h"

#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/RHI/Vulkan/VulkanCommandContext.h"
#include "Core/RHI/Vulkan/VulkanGraphicsPipeline.h"
#include "Core/RHI/Vulkan/VulkanProgram.h"
#include "Core/RHI/Vulkan/VulkanSampler.h"
#include "Core/RHI/Vulkan/VulkanSwapchain.h"
#include "Core/RHI/Vulkan/VulkanTexture.h"
#include "Core/Logging/Logger.h"

#if JBRO_RHI_VULKAN
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{
	VkFormat ToVulkanVertexFormat(ERHIVertexFormat format)
	{
		switch (format)
		{
		case ERHIVertexFormat::Float2:
			return VK_FORMAT_R32G32_SFLOAT;
		case ERHIVertexFormat::Float3:
			return VK_FORMAT_R32G32B32_SFLOAT;
		case ERHIVertexFormat::Float4:
		case ERHIVertexFormat::Color4:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		default:
			return VK_FORMAT_R32G32B32_SFLOAT;
		}
	}

	std::uint32_t GetVertexFormatSize(ERHIVertexFormat format)
	{
		switch (format)
		{
		case ERHIVertexFormat::Float2:
			return sizeof(float) * 2;
		case ERHIVertexFormat::Float3:
			return sizeof(float) * 3;
		case ERHIVertexFormat::Float4:
		case ERHIVertexFormat::Color4:
			return sizeof(float) * 4;
		default:
			return sizeof(float) * 3;
		}
	}

	VkPrimitiveTopology ToVulkanTopology(ERHIPrimitiveTopology topology)
	{
		switch (topology)
		{
		case ERHIPrimitiveTopology::TriangleStrip:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case ERHIPrimitiveTopology::LineList:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case ERHIPrimitiveTopology::LineStrip:
			return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		default:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	}

	bool IsInstanceLayerAvailable(const char* layerName)
	{
		std::uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> layers(layerCount);
		if (layerCount > 0)
		{
			vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
		}

		for (const VkLayerProperties& layer : layers)
		{
			if (std::strcmp(layer.layerName, layerName) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool IsInstanceExtensionAvailable(const char* extensionName)
	{
		std::uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensions(extensionCount);
		if (extensionCount > 0)
		{
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
		}

		for (const VkExtensionProperties& extension : extensions)
		{
			if (std::strcmp(extension.extensionName, extensionName) == 0)
			{
				return true;
			}
		}
		return false;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
		void*)
	{
		const char* message = callbackData && callbackData->pMessage ? callbackData->pMessage : "Vulkan validation message.";
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			Log::Error(message);
		}
		else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			Log::Warning(message);
		}
		else
		{
			Log::Debug(message);
		}
		return VK_FALSE;
	}
}
#endif

bool CVulkanRHIDevice::Initialize(const RHIDesc& desc)
{
	m_desc = desc;
#if JBRO_RHI_VULKAN
	const ERenderSurfaceType expectedSurfaceType =
#if JBRO_PLATFORM_WINDOWS
		ERenderSurfaceType::Win32Hwnd;
#else
		ERenderSurfaceType::MobileNativeSurface;
#endif
	if (expectedSurfaceType != desc.Surface.NativeHandle.SurfaceType || nullptr == desc.Surface.NativeHandle.Handle)
	{
		Log::Error("Vulkan init failed: invalid render surface handle.");
		return false;
	}

	if (false == CreateInstance())
	{
		Log::Error("Vulkan init failed: CreateInstance.");
		Finalize();
		return false;
	}
	if (false == CreateSurface())
	{
		Log::Error("Vulkan init failed: CreateSurface (vkCreateAndroidSurfaceKHR).");
		Finalize();
		return false;
	}
	if (false == SelectPhysicalDevice())
	{
		Log::Error("Vulkan init failed: SelectPhysicalDevice (no device with graphics+present).");
		Finalize();
		return false;
	}
	if (false == CreateLogicalDevice())
	{
		Log::Error("Vulkan init failed: CreateLogicalDevice.");
		Finalize();
		return false;
	}
	if (false == CreateCommandPool())
	{
		Log::Error("Vulkan init failed: CreateCommandPool.");
		Finalize();
		return false;
	}

	m_rhiSwapchain = MakeOwnerPtr<CVulkanSwapchain>();
	if (!m_rhiSwapchain || false == m_rhiSwapchain->Initialize(desc.Surface))
	{
		Log::Error("Vulkan init failed: Swapchain Initialize.");
		Finalize();
		return false;
	}
	if (false == static_cast<CVulkanSwapchain*>(m_rhiSwapchain.Get())->BindNativeSwapchain(
		m_instance, m_physicalDevice, m_device, m_surface, m_presentQueue, m_presentQueueFamily))
	{
		Log::Error("Vulkan init failed: BindNativeSwapchain.");
		Finalize();
		return false;
	}

	m_immediateCommandContext = MakeOwnerPtr<CVulkanCommandContext>();
	if (!m_immediateCommandContext)
	{
		Finalize();
		return false;
	}
	static_cast<CVulkanCommandContext*>(m_immediateCommandContext.Get())->BindNativeContext(
		m_device, m_graphicsQueue, m_commandPool, static_cast<CVulkanSwapchain*>(m_rhiSwapchain.Get()));
#endif
	m_isInitialized = true;
	return true;
}

void CVulkanRHIDevice::BeginFrame()
{
	if (m_immediateCommandContext)
	{
		m_immediateCommandContext->BeginFrame();
	}
}

void CVulkanRHIDevice::EndFrame()
{
	if (m_immediateCommandContext)
	{
		m_immediateCommandContext->EndFrame();
	}
	if (m_rhiSwapchain)
	{
		m_rhiSwapchain->Present();
	}
}

void CVulkanRHIDevice::Finalize()
{
	m_immediateCommandContext.Reset();
	if (m_rhiSwapchain)
	{
		static_cast<CVulkanSwapchain*>(m_rhiSwapchain.Get())->Finalize();
		m_rhiSwapchain.Reset();
	}
#if JBRO_RHI_VULKAN
	DestroyVulkanObjects();
#endif
	m_isInitialized = false;
}

void CVulkanRHIDevice::HandleSurfaceResize(const RenderSurfaceSize& size)
{
	m_desc.Surface.Size = size;
	if (m_rhiSwapchain)
	{
		m_rhiSwapchain->Resize(size);
	}
}

OwnerPtr<IRHIBuffer> CVulkanRHIDevice::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || desc.SizeInBytes == 0)
	{
		return nullptr;
	}

	VkBufferUsageFlags usage = 0;
	if (0 != (desc.BindFlags & static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer))) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (0 != (desc.BindFlags & static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer))) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (0 != (desc.BindFlags & static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer))) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	if (false == CreateNativeBuffer(desc.SizeInBytes, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, memory))
	{
		return nullptr;
	}
	if (initialData)
	{
		void* mapped = nullptr;
		vkMapMemory(m_device, memory, 0, desc.SizeInBytes, 0, &mapped);
		std::memcpy(mapped, initialData, desc.SizeInBytes);
		vkUnmapMemory(m_device, memory);
	}

	OwnerPtr<CVulkanBuffer> rhiBuffer = MakeOwnerPtr<CVulkanBuffer>(desc);
	rhiBuffer->BindNativeBuffer(m_device, buffer, memory);
	return rhiBuffer;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHITexture> CVulkanRHIDevice::CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData)
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || desc.Width == 0 || desc.Height == 0)
	{
		return nullptr;
	}
	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (0 != (desc.BindFlags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource))) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (0 != (desc.BindFlags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget))) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { desc.Width, desc.Height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkImage image = VK_NULL_HANDLE;
	if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
	{
		return nullptr;
	}

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements(m_device, image, &requirements);
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory memory = VK_NULL_HANDLE;
	if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
	{
		vkDestroyImage(m_device, image, nullptr);
		return nullptr;
	}
	vkBindImageMemory(m_device, image, memory, 0);

	if (initialData)
	{
		const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(desc.Width) * static_cast<VkDeviceSize>(desc.Height) * 4;
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
		if (false == CreateNativeBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory))
		{
			vkFreeMemory(m_device, memory, nullptr);
			vkDestroyImage(m_device, image, nullptr);
			return nullptr;
		}

		void* mapped = nullptr;
		if (vkMapMemory(m_device, stagingMemory, 0, uploadSize, 0, &mapped) == VK_SUCCESS)
		{
			std::memcpy(mapped, initialData, static_cast<std::size_t>(uploadSize));
			vkUnmapMemory(m_device, stagingMemory);
		}

		const bool copied = TransitionImageLayout(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			&& CopyBufferToImage(stagingBuffer, image, desc.Width, desc.Height)
			&& TransitionImageLayout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		vkDestroyBuffer(m_device, stagingBuffer, nullptr);
		vkFreeMemory(m_device, stagingMemory, nullptr);
		if (false == copied)
		{
			vkFreeMemory(m_device, memory, nullptr);
			vkDestroyImage(m_device, image, nullptr);
			return nullptr;
		}
	}
	else if (0 != (desc.BindFlags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource)))
	{
		TransitionImageLayout(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	VkImageView imageView = VK_NULL_HANDLE;
	if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
	{
		vkFreeMemory(m_device, memory, nullptr);
		vkDestroyImage(m_device, image, nullptr);
		return nullptr;
	}

	OwnerPtr<CVulkanTexture> texture = MakeOwnerPtr<CVulkanTexture>(desc);
	texture->BindNativeTexture(m_device, image, memory, imageView);
	if (0 != (desc.BindFlags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource)))
	{
		texture->SetCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	return texture;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHISampler> CVulkanRHIDevice::CreateSampler(const RHISamplerDesc& desc)
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE)
	{
		return nullptr;
	}

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = ERHIFilterMode::Point == desc.Filter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	samplerInfo.minFilter = samplerInfo.magFilter;
	samplerInfo.addressModeU = ERHIAddressMode::Repeat == desc.AddressU ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = ERHIAddressMode::Repeat == desc.AddressV ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
	{
		return nullptr;
	}

	OwnerPtr<CVulkanSampler> rhiSampler = MakeOwnerPtr<CVulkanSampler>(desc);
	rhiSampler->BindNativeSampler(m_device, sampler);
	return rhiSampler;
#else
	(void)desc;
	return nullptr;
#endif
}

OwnerPtr<IRHIProgram> CVulkanRHIDevice::CreateProgram(const RHIProgramDesc& desc)
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || desc.Language != ERHIProgramLanguage::SPIRV || nullptr == desc.Source)
	{
		return nullptr;
	}

	const std::size_t byteSize = desc.SourceSize;
	if (byteSize == 0 || 0 != (byteSize % sizeof(std::uint32_t)))
	{
		return nullptr;
	}

	VkShaderModuleCreateInfo moduleInfo = {};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = byteSize;
	moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(desc.Source);
	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
	{
		return nullptr;
	}

	OwnerPtr<CVulkanProgram> program = MakeOwnerPtr<CVulkanProgram>(desc.Stage, desc.Language);
	program->BindNativeShaderModule(m_device, module);
	return program;
#else
	(void)desc;
	return nullptr;
#endif
}

OwnerPtr<IRHIGraphicsPipeline> CVulkanRHIDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
#if JBRO_RHI_VULKAN
	if (m_device == VK_NULL_HANDLE || false == desc.VertexProgram.IsValid() || false == desc.PixelProgram.IsValid() || !m_rhiSwapchain)
	{
		return nullptr;
	}
	CVulkanProgram* vertexProgram = static_cast<CVulkanProgram*>(desc.VertexProgram.TryGet());
	CVulkanProgram* pixelProgram = static_cast<CVulkanProgram*>(desc.PixelProgram.TryGet());
	if (nullptr == vertexProgram || nullptr == pixelProgram || vertexProgram->GetShaderModule() == VK_NULL_HANDLE || pixelProgram->GetShaderModule() == VK_NULL_HANDLE)
	{
		return nullptr;
	}

	VkDescriptorSetLayoutBinding descriptorBindings[3] = {};
	descriptorBindings[0].binding = 0;
	descriptorBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorBindings[0].descriptorCount = 1;
	descriptorBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	descriptorBindings[1].binding = 1;
	descriptorBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	descriptorBindings[1].descriptorCount = 1;
	descriptorBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descriptorBindings[2].binding = 2;
	descriptorBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptorBindings[2].descriptorCount = 1;
	descriptorBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = 3;
	setLayoutInfo.pBindings = descriptorBindings;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	if (vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
	{
		return nullptr;
	}

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &descriptorSetLayout;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
	{
		vkDestroyDescriptorSetLayout(m_device, descriptorSetLayout, nullptr);
		return nullptr;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertexProgram->GetShaderModule();
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = pixelProgram->GetShaderModule();
	stages[1].pName = "main";

	std::vector<VkVertexInputAttributeDescription> attributes;
	std::vector<VkVertexInputBindingDescription> bindings;
	for (std::uint32_t i = 0; i < desc.VertexElementCount; ++i)
	{
		const RHIVertexElementDesc& element = desc.VertexElements[i];
		VkVertexInputAttributeDescription attribute = {};
		attribute.location = i;
		attribute.binding = element.InputSlot;
		attribute.format = ToVulkanVertexFormat(element.Format);
		attribute.offset = element.Offset;
		attributes.push_back(attribute);

		VkVertexInputBindingDescription* binding = nullptr;
		for (VkVertexInputBindingDescription& candidate : bindings)
		{
			if (candidate.binding == element.InputSlot)
			{
				binding = &candidate;
				break;
			}
		}
		if (nullptr == binding)
		{
			VkVertexInputBindingDescription newBinding = {};
			newBinding.binding = element.InputSlot;
			newBinding.inputRate = ERHIVertexInputRate::PerInstance == element.InputRate
				? VK_VERTEX_INPUT_RATE_INSTANCE
				: VK_VERTEX_INPUT_RATE_VERTEX;
			bindings.push_back(newBinding);
			binding = &bindings.back();
		}
		binding->stride = std::max(binding->stride, element.Offset + GetVertexFormatSize(element.Format));
	}

	std::sort(bindings.begin(), bindings.end(), [](const VkVertexInputBindingDescription& lhs, const VkVertexInputBindingDescription& rhs)
		{
			return lhs.binding < rhs.binding;
		});

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	if (!attributes.empty())
	{
		vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
		vertexInput.pVertexBindingDescriptions = bindings.data();
		vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
		vertexInput.pVertexAttributeDescriptions = attributes.data();
	}

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = ToVulkanTopology(desc.PrimitiveTopology);

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = desc.BlendMode != ERHIBlendMode::Opaque;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = desc.BlendMode == ERHIBlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = layout;
	pipelineInfo.renderPass = static_cast<CVulkanSwapchain*>(m_rhiSwapchain.Get())->GetRenderPass();
	pipelineInfo.subpass = 0;
	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
	{
		vkDestroyPipelineLayout(m_device, layout, nullptr);
		vkDestroyDescriptorSetLayout(m_device, descriptorSetLayout, nullptr);
		return nullptr;
	}

	OwnerPtr<CVulkanGraphicsPipeline> rhiPipeline = MakeOwnerPtr<CVulkanGraphicsPipeline>(desc);
	rhiPipeline->BindNativePipeline(m_device, descriptorSetLayout, layout, pipeline);
	return rhiPipeline;
#else
	(void)desc;
	return nullptr;
#endif
}

SafePtr<IRHISwapchain> CVulkanRHIDevice::GetSwapchain() const
{
	return m_rhiSwapchain.GetSafePtr();
}

SafePtr<IRHICommandContext> CVulkanRHIDevice::GetImmediateCommandContext() const
{
	return m_immediateCommandContext.GetSafePtr();
}

RHINativeDeviceDesc CVulkanRHIDevice::GetNativeDeviceDesc() const
{
	RHINativeDeviceDesc desc;
#if JBRO_RHI_VULKAN
	desc.Device = m_device;
	desc.DeviceContext = m_commandPool;
#endif
	return desc;
}

ERHIApi CVulkanRHIDevice::GetApi() const
{
	return ERHIApi::Vulkan;
}

const char* CVulkanRHIDevice::GetName() const
{
	return "Vulkan";
}

#if JBRO_RHI_VULKAN
bool CVulkanRHIDevice::CreateInstance()
{
	constexpr const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
	m_enableValidationLayer = m_desc.EnableDebugLayer && IsInstanceLayerAvailable(validationLayerName);
	m_enableDebugUtils = m_desc.EnableDebugLayer && IsInstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME };
#if JBRO_PLATFORM_ANDROID
	extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
#if JBRO_PLATFORM_IOS
	extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
#if JBRO_PLATFORM_WINDOWS
	extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
	if (m_enableDebugUtils)
	{
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	std::vector<const char*> layers;
	if (m_enableValidationLayer)
	{
		layers.push_back(validationLayerName);
	}

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "JBroEngine";
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
	createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
#if JBRO_PLATFORM_IOS
	createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
	if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
	{
		return false;
	}
	return CreateDebugMessenger();
}

bool CVulkanRHIDevice::CreateDebugMessenger()
{
	if (false == m_enableDebugUtils)
	{
		return true;
	}

	auto createDebugUtilsMessenger =
		reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
	if (nullptr == createDebugUtilsMessenger)
	{
		return true;
	}

	VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = VulkanDebugCallback;

	if (createDebugUtilsMessenger(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS)
	{
		m_debugMessenger = VK_NULL_HANDLE;
	}
	return true;
}

bool CVulkanRHIDevice::CreateSurface()
{
#if JBRO_PLATFORM_ANDROID
	VkAndroidSurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.window = static_cast<ANativeWindow*>(m_desc.Surface.NativeHandle.Handle);
	return vkCreateAndroidSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface) == VK_SUCCESS;
#elif JBRO_PLATFORM_IOS
	VkMetalSurfaceCreateInfoEXT surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
	surfaceInfo.pLayer = m_desc.Surface.NativeHandle.Handle;
	return vkCreateMetalSurfaceEXT(m_instance, &surfaceInfo, nullptr, &m_surface) == VK_SUCCESS;
#elif JBRO_PLATFORM_WINDOWS
	VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.hinstance = GetModuleHandleW(nullptr);
	surfaceInfo.hwnd = static_cast<HWND>(m_desc.Surface.NativeHandle.Handle);
	return vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface) == VK_SUCCESS;
#else
	return false;
#endif
}

bool CVulkanRHIDevice::SelectPhysicalDevice()
{
	std::uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
	Log::Info(("Vulkan: physical device count = " + std::to_string(deviceCount)).c_str());
	if (deviceCount == 0)
	{
		return false;
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

	// 선호: graphics+present 를 모두 지원하는 단일 큐 패밀리. 없으면 graphics 패밀리와
	// present 패밀리가 따로여도 허용한다(에뮬레이터/일부 드라이버는 분리되어 있다).
	constexpr std::uint32_t kInvalidFamily = std::numeric_limits<std::uint32_t>::max();
	for (VkPhysicalDevice device : devices)
	{
		std::uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

		std::uint32_t graphicsFamily = kInvalidFamily;
		std::uint32_t presentFamily = kInvalidFamily;
		std::uint32_t combinedFamily = kInvalidFamily;
		for (std::uint32_t i = 0; i < queueFamilyCount; ++i)
		{
			const bool hasGraphics = 0 != (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT);
			VkBool32 presentSupport = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

			if (hasGraphics && kInvalidFamily == graphicsFamily)
			{
				graphicsFamily = i;
			}
			if (VK_TRUE == presentSupport && kInvalidFamily == presentFamily)
			{
				presentFamily = i;
			}
			if (hasGraphics && VK_TRUE == presentSupport)
			{
				combinedFamily = i;
				break;
			}
		}

		if (kInvalidFamily != combinedFamily)
		{
			m_physicalDevice = device;
			m_graphicsQueueFamily = combinedFamily;
			m_presentQueueFamily = combinedFamily;
			return true;
		}
		if (kInvalidFamily != graphicsFamily && kInvalidFamily != presentFamily)
		{
			Log::Warning("Vulkan: using separate graphics/present queue families.");
			m_physicalDevice = device;
			m_graphicsQueueFamily = graphicsFamily;
			m_presentQueueFamily = presentFamily;
			return true;
		}

		Log::Warning(("Vulkan: device rejected (graphics family "
			+ std::string(kInvalidFamily == graphicsFamily ? "missing" : "ok")
			+ ", present family "
			+ std::string(kInvalidFamily == presentFamily ? "missing" : "ok") + ").").c_str());
	}
	return false;
}

bool CVulkanRHIDevice::CreateLogicalDevice()
{
	const float priority = 1.0f;
	// graphics/present 패밀리가 같으면 큐 1개, 다르면 패밀리별로 큐를 만든다.
	std::set<std::uint32_t> uniqueFamilies = { m_graphicsQueueFamily, m_presentQueueFamily };
	std::vector<VkDeviceQueueCreateInfo> queueInfos;
	queueInfos.reserve(uniqueFamilies.size());
	for (std::uint32_t family : uniqueFamilies)
	{
		VkDeviceQueueCreateInfo queueInfo = {};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = family;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;
		queueInfos.push_back(queueInfo);
	}

	std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#if JBRO_PLATFORM_IOS
	extensions.push_back("VK_KHR_portability_subset");
#endif

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
	createInfo.pQueueCreateInfos = queueInfos.data();
	createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
	if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
	{
		return false;
	}

	vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
	vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
	return true;
}

bool CVulkanRHIDevice::CreateCommandPool()
{
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
	return vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) == VK_SUCCESS;
}

void CVulkanRHIDevice::DestroyVulkanObjects()
{
	if (m_device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(m_device);
		if (m_commandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
			m_commandPool = VK_NULL_HANDLE;
		}
		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
	}
	if (m_instance != VK_NULL_HANDLE && m_surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
		m_surface = VK_NULL_HANDLE;
	}
	if (m_instance != VK_NULL_HANDLE && m_debugMessenger != VK_NULL_HANDLE)
	{
		auto destroyDebugUtilsMessenger =
			reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (destroyDebugUtilsMessenger)
		{
			destroyDebugUtilsMessenger(m_instance, m_debugMessenger, nullptr);
		}
		m_debugMessenger = VK_NULL_HANDLE;
	}
	if (m_instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;
	}
	m_physicalDevice = VK_NULL_HANDLE;
	m_graphicsQueue = VK_NULL_HANDLE;
	m_presentQueue = VK_NULL_HANDLE;
	m_graphicsQueueFamily = UINT32_MAX;
	m_presentQueueFamily = UINT32_MAX;
	m_enableValidationLayer = false;
	m_enableDebugUtils = false;
}

std::uint32_t CVulkanRHIDevice::FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
	for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	return 0;
}

bool CVulkanRHIDevice::CreateNativeBuffer(std::size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& outBuffer, VkDeviceMemory& outMemory) const
{
	if (m_device == VK_NULL_HANDLE || size == 0)
	{
		return false;
	}

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = static_cast<VkDeviceSize>(size);
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
	{
		return false;
	}

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(m_device, outBuffer, &requirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);

	if (vkAllocateMemory(m_device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
	{
		vkDestroyBuffer(m_device, outBuffer, nullptr);
		outBuffer = VK_NULL_HANDLE;
		return false;
	}

	vkBindBufferMemory(m_device, outBuffer, outMemory, 0);
	return true;
}

bool CVulkanRHIDevice::SubmitImmediate(const std::function<void(VkCommandBuffer)>& record) const
{
	if (m_device == VK_NULL_HANDLE || m_commandPool == VK_NULL_HANDLE || m_graphicsQueue == VK_NULL_HANDLE)
	{
		return false;
	}

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer) != VK_SUCCESS)
	{
		return false;
	}

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	record(commandBuffer);
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	const bool submitted = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS;
	if (submitted)
	{
		vkQueueWaitIdle(m_graphicsQueue);
	}
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
	return submitted;
}

bool CVulkanRHIDevice::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const
{
	if (image == VK_NULL_HANDLE)
	{
		return false;
	}

	return SubmitImmediate([image, oldLayout, newLayout](VkCommandBuffer commandBuffer)
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}

		vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	});
}

bool CVulkanRHIDevice::CopyBufferToImage(VkBuffer buffer, VkImage image, std::uint32_t width, std::uint32_t height) const
{
	if (buffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE || width == 0 || height == 0)
	{
		return false;
	}

	return SubmitImmediate([buffer, image, width, height](VkCommandBuffer commandBuffer)
	{
		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { width, height, 1 };
		vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	});
}
#endif
