#include "pch.h"
#include "VulkanRHIDevice.h"

#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/RHI/Vulkan/VulkanCommandContext.h"
#include "Core/RHI/Vulkan/VulkanGraphicsPipeline.h"
#include "Core/RHI/Vulkan/VulkanProgram.h"
#include "Core/RHI/Vulkan/VulkanSampler.h"
#include "Core/RHI/Vulkan/VulkanSwapchain.h"
#include "Core/RHI/Vulkan/VulkanTexture.h"

#if JBRO_PLATFORM_MOBILE
#include <cstring>
#include <vector>
#endif

bool CVulkanRHIDevice::Initialize(const RHIDesc& desc)
{
	m_desc = desc;
#if JBRO_PLATFORM_MOBILE
	if (ERenderSurfaceType::MobileNativeSurface != desc.Surface.NativeHandle.SurfaceType || nullptr == desc.Surface.NativeHandle.Handle)
	{
		return false;
	}

	if (false == CreateInstance()
		|| false == CreateSurface()
		|| false == SelectPhysicalDevice()
		|| false == CreateLogicalDevice()
		|| false == CreateCommandPool())
	{
		Finalize();
		return false;
	}

	m_rhiSwapchain = MakeOwnerPtr<CVulkanSwapchain>();
	if (!m_rhiSwapchain || false == m_rhiSwapchain->Initialize(desc.Surface))
	{
		Finalize();
		return false;
	}
	if (false == static_cast<CVulkanSwapchain*>(m_rhiSwapchain.Get())->BindNativeSwapchain(
		m_instance, m_physicalDevice, m_device, m_surface, m_presentQueue, m_presentQueueFamily))
	{
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
#if JBRO_PLATFORM_MOBILE
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
#if JBRO_PLATFORM_MOBILE
	if (m_device == VK_NULL_HANDLE || desc.SizeInBytes == 0)
	{
		return nullptr;
	}

	VkBufferUsageFlags usage = 0;
	if (0 != (desc.BindFlags & static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer))) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (0 != (desc.BindFlags & static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer))) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (0 != (desc.BindFlags & static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer))) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = desc.SizeInBytes;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer buffer = VK_NULL_HANDLE;
	if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
	{
		return nullptr;
	}

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(m_device, buffer, &requirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
	{
		vkDestroyBuffer(m_device, buffer, nullptr);
		return nullptr;
	}
	if (initialData)
	{
		void* mapped = nullptr;
		vkMapMemory(m_device, memory, 0, desc.SizeInBytes, 0, &mapped);
		std::memcpy(mapped, initialData, desc.SizeInBytes);
		vkUnmapMemory(m_device, memory);
	}
	vkBindBufferMemory(m_device, buffer, memory, 0);

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
#if JBRO_PLATFORM_MOBILE
	if (m_device == VK_NULL_HANDLE || desc.Width == 0 || desc.Height == 0)
	{
		return nullptr;
	}
	(void)initialData;

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
	return texture;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHISampler> CVulkanRHIDevice::CreateSampler(const RHISamplerDesc& desc)
{
#if JBRO_PLATFORM_MOBILE
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
#if JBRO_PLATFORM_MOBILE
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
#if JBRO_PLATFORM_MOBILE
	if (m_device == VK_NULL_HANDLE || false == desc.VertexProgram.IsValid() || false == desc.PixelProgram.IsValid() || false == m_rhiSwapchain.IsValid())
	{
		return nullptr;
	}
	CVulkanProgram* vertexProgram = static_cast<CVulkanProgram*>(desc.VertexProgram.TryGet());
	CVulkanProgram* pixelProgram = static_cast<CVulkanProgram*>(desc.PixelProgram.TryGet());
	if (nullptr == vertexProgram || nullptr == pixelProgram || vertexProgram->GetShaderModule() == VK_NULL_HANDLE || pixelProgram->GetShaderModule() == VK_NULL_HANDLE)
	{
		return nullptr;
	}

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
	{
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

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
		return nullptr;
	}

	OwnerPtr<CVulkanGraphicsPipeline> rhiPipeline = MakeOwnerPtr<CVulkanGraphicsPipeline>(desc);
	rhiPipeline->BindNativePipeline(m_device, layout, pipeline);
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
#if JBRO_PLATFORM_MOBILE
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

#if JBRO_PLATFORM_MOBILE
bool CVulkanRHIDevice::CreateInstance()
{
	std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME };
#if JBRO_PLATFORM_ANDROID
	extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
#if JBRO_PLATFORM_IOS
	extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "JBroEngine";
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
#if JBRO_PLATFORM_IOS
	createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
	return vkCreateInstance(&createInfo, nullptr, &m_instance) == VK_SUCCESS;
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
#else
	return false;
#endif
}

bool CVulkanRHIDevice::SelectPhysicalDevice()
{
	std::uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
	if (deviceCount == 0)
	{
		return false;
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
	for (VkPhysicalDevice device : devices)
	{
		std::uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());
		for (std::uint32_t i = 0; i < queueFamilyCount; ++i)
		{
			VkBool32 presentSupport = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
			if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
			{
				m_physicalDevice = device;
				m_graphicsQueueFamily = i;
				m_presentQueueFamily = i;
				return true;
			}
		}
	}
	return false;
}

bool CVulkanRHIDevice::CreateLogicalDevice()
{
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = m_graphicsQueueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#if JBRO_PLATFORM_IOS
	extensions.push_back("VK_KHR_portability_subset");
#endif

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;
	createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
	if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
	{
		return false;
	}

	vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
	m_presentQueue = m_graphicsQueue;
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
#endif
