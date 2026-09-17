#include "VulkanDevice.h"

#include <cstring>

namespace JBro::Internal
{
    namespace
    {
        constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT types,
            const VkDebugUtilsMessengerCallbackDataEXT* data,
            void* user)
        {
            static_cast<void>(types);
            static_cast<void>(data);
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 && user != nullptr)
            {
                static_cast<VulkanDevice*>(user)->CountValidationError();
            }
            return VK_FALSE;
        }

        bool HasLayer(const char* name)
        {
            std::uint32_t count = 0;
            if (vk.vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0 || count > 256)
            {
                return false;
            }
            VkLayerProperties layers[256];
            if (vk.vkEnumerateInstanceLayerProperties(&count, layers) != VK_SUCCESS)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (std::strcmp(layers[index].layerName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasInstanceExtension(const char* name)
        {
            std::uint32_t count = 0;
            if (vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0
                || count > 256)
            {
                return false;
            }
            VkExtensionProperties extensions[256];
            if (vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions) != VK_SUCCESS)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (std::strcmp(extensions[index].extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
        {
            std::uint32_t count = 0;
            if (vk.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
            {
                return false;
            }
            // 드라이버는 확장을 수백 개 든다. 512 개까지만 본다 - 스왑체인 확장은 어느 드라이버든 앞쪽에 있다.
            VkExtensionProperties extensions[512];
            if (count > 512)
            {
                count = 512;
            }
            const VkResult result = vk.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions);
            if (result != VK_SUCCESS && result != VK_INCOMPLETE)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (std::strcmp(extensions[index].extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        VkPresentModeKHR PickPresentMode(VkPhysicalDevice device, VkSurfaceKHR surface, PresentMode wanted)
        {
            if (wanted == PresentMode::VSync)
            {
                return VK_PRESENT_MODE_FIFO_KHR;
            }
            std::uint32_t count = 0;
            VkPresentModeKHR modes[16];
            if (vk.vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr) != VK_SUCCESS)
            {
                return VK_PRESENT_MODE_FIFO_KHR;
            }
            if (count > 16)
            {
                count = 16;
            }
            if (vk.vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes) != VK_SUCCESS)
            {
                return VK_PRESENT_MODE_FIFO_KHR;
            }
            bool mailbox = false;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (modes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                    return VK_PRESENT_MODE_IMMEDIATE_KHR;
                }
                mailbox = mailbox || modes[index] == VK_PRESENT_MODE_MAILBOX_KHR;
            }
            return mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
        }
    }

    std::uint32_t VulkanNextGeneration(std::uint32_t generation)
    {
        ++generation;
        if (generation == 0)
        {
            generation = 1;
        }
        return generation;
    }

    void VulkanDevice::CountValidationError()
    {
        ++m_validationErrors;
    }

    // ── 초기화 ──────────────────────────────────────────────────────────────

    bool VulkanDevice::Initialize(const RHIDeviceCreateInfo& createInfo)
    {
        if (m_device != VK_NULL_HANDLE || m_instance != VK_NULL_HANDLE)
        {
            return false;
        }
        if (false == LoadVulkanLibrary() || false == CreateInstance(createInfo.enableValidation)
            || false == PickPhysicalDevice() || false == CreateLogicalDevice() || false == CreateFrameSlots())
        {
            Shutdown();
            return false;
        }
        m_commandContext.Bind(this);
        m_status = FrameStatus::Ready;
        return true;
    }

    bool VulkanDevice::CreateInstance(bool validation)
    {
        std::uint32_t apiVersion = VK_API_VERSION_1_0;
        if (vk.vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS || apiVersion < VK_API_VERSION_1_3)
        {
            return false;
        }
        const char* extensions[4] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        std::uint32_t extensionCount = 2;
        const char* layers[1] = {};
        std::uint32_t layerCount = 0;
        const bool debugUtils = validation && HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (debugUtils)
        {
            extensions[extensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
        // 레이어가 없는 기계면 검증 없이 간다 - 검증 하나 때문에 디바이스가 없으면 안 된다. 그때
        // `GetValidationErrorCount` 의 0 은 "조용했다" 가 아니라 "못 들었다" 다.
        if (validation && HasLayer(ValidationLayerName))
        {
            layers[layerCount++] = ValidationLayerName;
        }
        VkApplicationInfo application = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "JBroEngine";
        application.pEngineName = "JBroEngine";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = extensionCount;
        info.ppEnabledExtensionNames = extensions;
        info.enabledLayerCount = layerCount;
        info.ppEnabledLayerNames = layers;
        if (vk.vkCreateInstance(&info, nullptr, &m_instance) != VK_SUCCESS)
        {
            m_instance = VK_NULL_HANDLE;
            return false;
        }
        if (false == LoadVulkanInstanceFunctions(m_instance))
        {
            return false;
        }
        if (debugUtils && vk.vkCreateDebugUtilsMessengerEXT != nullptr)
        {
            VkDebugUtilsMessengerCreateInfoEXT messenger = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            messenger.pfnUserCallback = DebugCallback;
            messenger.pUserData = this;
            if (vk.vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger) != VK_SUCCESS)
            {
                m_messenger = VK_NULL_HANDLE;
            }
        }
        return true;
    }

    bool VulkanDevice::PickPhysicalDevice()
    {
        std::uint32_t count = 0;
        VkPhysicalDevice devices[16];
        if (vk.vkEnumeratePhysicalDevices(m_instance, &count, nullptr) != VK_SUCCESS || count == 0)
        {
            return false;
        }
        if (count > 16)
        {
            count = 16;
        }
        if (vk.vkEnumeratePhysicalDevices(m_instance, &count, devices) < VK_SUCCESS)
        {
            return false;
        }
        // 외장을 먼저, 그다음 아무것이나. 조건은 넷이다: 1.3, 동적 렌더링, synchronization2, 제시 가능한 그래픽 큐.
        VkPhysicalDevice chosen = VK_NULL_HANDLE;
        std::uint32_t chosenFamily = 0;
        bool chosenDiscrete = false;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            VkPhysicalDeviceProperties properties = {};
            vk.vkGetPhysicalDeviceProperties(devices[index], &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3
                || false == HasDeviceExtension(devices[index], VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                continue;
            }
            VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &features13;
            vk.vkGetPhysicalDeviceFeatures2(devices[index], &features);
            if (features13.dynamicRendering == VK_FALSE || features13.synchronization2 == VK_FALSE)
            {
                continue;
            }
            std::uint32_t familyCount = 0;
            VkQueueFamilyProperties families[16];
            vk.vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &familyCount, nullptr);
            if (familyCount > 16)
            {
                familyCount = 16;
            }
            vk.vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &familyCount, families);
            std::uint32_t family = familyCount;
            for (std::uint32_t at = 0; at < familyCount; ++at)
            {
                if ((families[at].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0
                    && vk.vkGetPhysicalDeviceWin32PresentationSupportKHR(devices[index], at) == VK_TRUE)
                {
                    family = at;
                    break;
                }
            }
            if (family == familyCount)
            {
                continue;
            }
            const bool discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (chosen == VK_NULL_HANDLE || (discrete && false == chosenDiscrete))
            {
                chosen = devices[index];
                chosenFamily = family;
                chosenDiscrete = discrete;
            }
        }
        if (chosen == VK_NULL_HANDLE)
        {
            return false;
        }
        m_physicalDevice = chosen;
        m_queueFamily = chosenFamily;
        vk.vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
        return true;
    }

    bool VulkanDevice::CreateLogicalDevice()
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = m_queueFamily;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features13;
        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.pNext = &features;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        if (vk.vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device) != VK_SUCCESS)
        {
            m_device = VK_NULL_HANDLE;
            return false;
        }
        if (false == LoadVulkanDeviceFunctions(m_device))
        {
            return false;
        }
        vk.vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
        VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = m_queueFamily;
        if (vk.vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool) != VK_SUCCESS)
        {
            m_commandPool = VK_NULL_HANDLE;
            return false;
        }
        VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = m_commandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (vk.vkAllocateCommandBuffers(m_device, &allocate, &m_oneShotCommands) != VK_SUCCESS)
        {
            return false;
        }
        VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        return vk.vkCreateFence(m_device, &fence, nullptr, &m_oneShotFence) == VK_SUCCESS;
    }

    bool VulkanDevice::CreateFrameSlots()
    {
        for (VulkanFrameSlot& slot : m_slots)
        {
            VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = m_commandPool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            // 처음에는 신호 상태다. 첫 프레임이 기다릴 지난 프레임이 없다.
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            // 드로우마다 set 하나다. UI 는 드로우가 수백이라 넉넉히 둔다.
            const VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096 * MaxBoundTextures},
                {VK_DESCRIPTOR_TYPE_SAMPLER, 4096 * MaxBoundSamplers}};
            VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool.maxSets = 4096;
            pool.poolSizeCount = 2;
            pool.pPoolSizes = sizes;
            if (vk.vkAllocateCommandBuffers(m_device, &allocate, &slot.commands) != VK_SUCCESS
                || vk.vkCreateFence(m_device, &fence, nullptr, &slot.fence) != VK_SUCCESS
                || vk.vkCreateSemaphore(m_device, &semaphore, nullptr, &slot.imageAvailable) != VK_SUCCESS
                || vk.vkCreateDescriptorPool(m_device, &pool, nullptr, &slot.descriptorPool) != VK_SUCCESS)
            {
                return false;
            }
        }
        return true;
    }

    void VulkanDevice::DestroyFrameSlots()
    {
        for (VulkanFrameSlot& slot : m_slots)
        {
            if (slot.descriptorPool != VK_NULL_HANDLE)
            {
                vk.vkDestroyDescriptorPool(m_device, slot.descriptorPool, nullptr);
            }
            if (slot.imageAvailable != VK_NULL_HANDLE)
            {
                vk.vkDestroySemaphore(m_device, slot.imageAvailable, nullptr);
            }
            if (slot.fence != VK_NULL_HANDLE)
            {
                vk.vkDestroyFence(m_device, slot.fence, nullptr);
            }
            slot = {};
        }
    }

    void VulkanDevice::Shutdown()
    {
        if (m_device != VK_NULL_HANDLE && (vk.vkDeviceWaitIdle == nullptr || vk.vkDestroyDevice == nullptr))
        {
            // 함수표가 다 실리지 않았다(`LoadVulkanDeviceFunctions` 실패). 지울 함수가 없으니 디바이스는 두고
            // 인스턴스만 내린다 - 초기화 실패 경로라 프로세스는 곧 Vulkan 없이 간다.
            m_device = VK_NULL_HANDLE;
        }
        if (m_device != VK_NULL_HANDLE)
        {
            vk.vkDeviceWaitIdle(m_device);
        }
        m_frameActive = false;
        m_oneShotActive = false;
        m_commandContext.Reset();
        if (m_device != VK_NULL_HANDLE)
        {
            for (std::uint32_t index = 0; index < MaxBuffers; ++index)
            {
                if (m_buffers[index].occupied)
                {
                    DestroyBuffer(BufferHandle{index, m_buffers[index].generation});
                }
            }
            for (std::uint32_t index = 0; index < MaxTextures; ++index)
            {
                if (m_textures[index].occupied)
                {
                    DestroyTexture(TextureHandle{TextureResourceBase + index, m_textures[index].generation});
                }
            }
            for (std::uint32_t index = 0; index < MaxSamplers; ++index)
            {
                if (m_samplers[index].occupied)
                {
                    DestroySampler(SamplerHandle{index, m_samplers[index].generation});
                }
            }
            for (std::uint32_t index = 0; index < MaxGraphicsPipelines; ++index)
            {
                if (m_graphicsPipelines[index].occupied)
                {
                    DestroyGraphicsPipeline(GraphicsPipelineHandle{index, m_graphicsPipelines[index].generation});
                }
            }
            FlushRetired(~std::uint64_t{0});
            for (std::uint32_t index = 0; index < MaxSwapchains; ++index)
            {
                if (m_swapchains[index].occupied)
                {
                    DestroySwapchain(SwapchainHandle{index, m_swapchains[index].generation});
                }
            }
            DestroyFrameSlots();
            if (m_oneShotFence != VK_NULL_HANDLE)
            {
                vk.vkDestroyFence(m_device, m_oneShotFence, nullptr);
                m_oneShotFence = VK_NULL_HANDLE;
            }
            if (m_commandPool != VK_NULL_HANDLE)
            {
                vk.vkDestroyCommandPool(m_device, m_commandPool, nullptr);
                m_commandPool = VK_NULL_HANDLE;
            }
            vk.vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        m_oneShotCommands = VK_NULL_HANDLE;
        m_queue = VK_NULL_HANDLE;
        if (m_instance != VK_NULL_HANDLE)
        {
            if (m_messenger != VK_NULL_HANDLE && vk.vkDestroyDebugUtilsMessengerEXT != nullptr)
            {
                vk.vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
            }
            if (vk.vkDestroyInstance != nullptr)
            {
                vk.vkDestroyInstance(m_instance, nullptr);
            }
        }
        m_messenger = VK_NULL_HANDLE;
        m_instance = VK_NULL_HANDLE;
        m_physicalDevice = VK_NULL_HANDLE;
        m_status = FrameStatus::InvalidState;
    }

    void VulkanDevice::MarkDeviceLost()
    {
        m_status = FrameStatus::DeviceLost;
        m_frameActive = false;
    }

    // ── 미룬 파기 ───────────────────────────────────────────────────────────

    void VulkanDevice::Retire(VulkanRetiredObject::Kind kind, std::uint64_t handle)
    {
        if (handle == 0)
        {
            return;
        }
        if (m_retiredCount == MaxRetired)
        {
            // 넘치면 한 번 기다리고 비운다. 드문 일이라 여기서 GPU 를 기다리는 것을 받아들인다. 기록 중인 프레임이
            // 은퇴시킨 것은 그 명령 버퍼가 아직 가리키므로 남긴다 - 그래도 자리가 없으면 이 객체는 새는 쪽을 택한다.
            if (m_device != VK_NULL_HANDLE)
            {
                vk.vkDeviceWaitIdle(m_device);
            }
            FlushRetired(m_frameActive ? m_frameSerial - 1 : ~std::uint64_t{0});
            if (m_retiredCount == MaxRetired)
            {
                return;
            }
        }
        m_retired[m_retiredCount++] = {kind, handle, m_frameSerial};
    }

    void VulkanDevice::DestroyRetired(const VulkanRetiredObject& object)
    {
        using Kind = VulkanRetiredObject::Kind;
        switch (object.kind)
        {
        case Kind::Buffer:
            vk.vkDestroyBuffer(m_device, reinterpret_cast<VkBuffer>(object.handle), nullptr);
            break;
        case Kind::Image:
            vk.vkDestroyImage(m_device, reinterpret_cast<VkImage>(object.handle), nullptr);
            break;
        case Kind::ImageView:
            vk.vkDestroyImageView(m_device, reinterpret_cast<VkImageView>(object.handle), nullptr);
            break;
        case Kind::Sampler:
            vk.vkDestroySampler(m_device, reinterpret_cast<VkSampler>(object.handle), nullptr);
            break;
        case Kind::Pipeline:
            vk.vkDestroyPipeline(m_device, reinterpret_cast<VkPipeline>(object.handle), nullptr);
            break;
        case Kind::PipelineLayout:
            vk.vkDestroyPipelineLayout(m_device, reinterpret_cast<VkPipelineLayout>(object.handle), nullptr);
            break;
        case Kind::DescriptorSetLayout:
            vk.vkDestroyDescriptorSetLayout(m_device, reinterpret_cast<VkDescriptorSetLayout>(object.handle), nullptr);
            break;
        case Kind::Memory:
            vk.vkFreeMemory(m_device, reinterpret_cast<VkDeviceMemory>(object.handle), nullptr);
            break;
        }
    }

    void VulkanDevice::FlushRetired(std::uint64_t completedSerial)
    {
        std::uint32_t kept = 0;
        for (std::uint32_t index = 0; index < m_retiredCount; ++index)
        {
            // 이번 프레임(m_frameSerial) 에 은퇴한 것은 그 프레임이 끝나야 지운다. 완료 번호가 은퇴 번호 이상이면 끝난 것이다.
            if (m_retired[index].serial <= completedSerial)
            {
                DestroyRetired(m_retired[index]);
            }
            else
            {
                m_retired[kept++] = m_retired[index];
            }
        }
        m_retiredCount = kept;
    }

    // ── 한 번짜리 명령 ──────────────────────────────────────────────────────

    bool VulkanDevice::BeginOneShot()
    {
        if (m_device == VK_NULL_HANDLE || m_oneShotActive || m_frameActive)
        {
            return false;
        }
        if (vk.vkResetCommandBuffer(m_oneShotCommands, 0) != VK_SUCCESS)
        {
            return false;
        }
        VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vk.vkBeginCommandBuffer(m_oneShotCommands, &begin) != VK_SUCCESS)
        {
            return false;
        }
        m_oneShotActive = true;
        return true;
    }

    bool VulkanDevice::EndOneShot()
    {
        if (false == m_oneShotActive)
        {
            return false;
        }
        m_oneShotActive = false;
        if (vk.vkEndCommandBuffer(m_oneShotCommands) != VK_SUCCESS)
        {
            return false;
        }
        VkCommandBufferSubmitInfo commands = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commands.commandBuffer = m_oneShotCommands;
        VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commands;
        if (vk.vkResetFences(m_device, 1, &m_oneShotFence) != VK_SUCCESS)
        {
            return false;
        }
        const VkResult result = vk.vkQueueSubmit2(m_queue, 1, &submit, m_oneShotFence);
        if (result == VK_ERROR_DEVICE_LOST)
        {
            MarkDeviceLost();
            return false;
        }
        if (result != VK_SUCCESS)
        {
            return false;
        }
        return vk.vkWaitForFences(m_device, 1, &m_oneShotFence, VK_TRUE, ~std::uint64_t{0}) == VK_SUCCESS;
    }

    void VulkanDevice::TransitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
        VkImageLayout from, VkImageLayout to)
    {
        if (from == to)
        {
            return;
        }
        VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        // 출발 쪽은 보수적으로 잡는다: 앞의 모든 명령의 모든 쓰기. 도착 쪽은 레이아웃이 말해 준다.
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        switch (to)
        {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = 0;
            break;
        default:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        }
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vk.vkCmdPipelineBarrier2(commands, &dependency);
    }

    // ── 스왑체인 ────────────────────────────────────────────────────────────

    VulkanSwapchainState* VulkanDevice::FindSwapchain(SwapchainHandle swapchain)
    {
        if (false == swapchain.IsValid() || swapchain.index >= MaxSwapchains)
        {
            return nullptr;
        }
        VulkanSwapchainState& state = m_swapchains[swapchain.index];
        if (false == state.occupied || state.generation != swapchain.generation)
        {
            return nullptr;
        }
        return &state;
    }

    void VulkanDevice::ReleaseSwapchainImages(VulkanSwapchainState& state)
    {
        for (std::uint32_t index = 0; index < MaxSwapchainImages; ++index)
        {
            if (state.views[index] != VK_NULL_HANDLE)
            {
                vk.vkDestroyImageView(m_device, state.views[index], nullptr);
            }
            if (state.renderFinished[index] != VK_NULL_HANDLE)
            {
                vk.vkDestroySemaphore(m_device, state.renderFinished[index], nullptr);
            }
            state.views[index] = VK_NULL_HANDLE;
            state.renderFinished[index] = VK_NULL_HANDLE;
            state.images[index] = VK_NULL_HANDLE;
            state.layouts[index] = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        if (state.presentedCopy != VK_NULL_HANDLE)
        {
            vk.vkDestroyImage(m_device, state.presentedCopy, nullptr);
            state.presentedCopy = VK_NULL_HANDLE;
        }
        if (state.presentedCopyMemory != VK_NULL_HANDLE)
        {
            vk.vkFreeMemory(m_device, state.presentedCopyMemory, nullptr);
            state.presentedCopyMemory = VK_NULL_HANDLE;
        }
        state.presentedCopyLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        state.imageCount = 0;
    }

    bool VulkanDevice::BuildSwapchain(VulkanSwapchainState& state, VkSwapchainKHR old)
    {
        VkSurfaceCapabilitiesKHR caps = {};
        if (vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, state.surface, &caps) != VK_SUCCESS)
        {
            return false;
        }
        std::uint32_t formatCount = 0;
        VkSurfaceFormatKHR formats[32];
        if (vk.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, state.surface, &formatCount, nullptr) != VK_SUCCESS
            || formatCount == 0)
        {
            return false;
        }
        if (formatCount > 32)
        {
            formatCount = 32;
        }
        if (vk.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, state.surface, &formatCount, formats) < VK_SUCCESS)
        {
            return false;
        }
        const VkFormat wanted = ToVulkanFormat(state.desc.format);
        bool supported = false;
        for (std::uint32_t index = 0; index < formatCount; ++index)
        {
            supported = supported || formats[index].format == wanted;
        }
        if (false == supported)
        {
            // 요청한 포맷이 없는 표면이다. 계약은 요청한 포맷의 백버퍼라 다른 것으로 바꿔치지 않는다.
            return false;
        }
        // 이미지 크기는 표면이 정한다(Windows 는 늘 창의 클라이언트 크기다). 계약의 크기(`desc.extent`)는 요청값으로
        // 남기고 그 안에서만 그리고 되읽는다 - DXGI 는 요청한 크기로 만들어 늘려 주지만 Vulkan 은 그러지 않는다.
        // 창의 최소 너비(120) 때문에 64 짝 창의 표면이 120 인 것이 그 예다. 요청이 표면보다 크면 만들 수 없다.
        VkExtent2D extent = {state.desc.extent.width, state.desc.extent.height};
        if (caps.currentExtent.width != 0xFFFFFFFFu)
        {
            if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0
                || caps.currentExtent.width < extent.width || caps.currentExtent.height < extent.height)
            {
                return false;
            }
            extent = caps.currentExtent;
        }
        else
        {
            if (extent.width < caps.minImageExtent.width || extent.height < caps.minImageExtent.height
                || extent.width > caps.maxImageExtent.width || extent.height > caps.maxImageExtent.height)
            {
                return false;
            }
        }
        std::uint32_t imageCount = state.desc.bufferCount;
        if (imageCount < caps.minImageCount)
        {
            imageCount = caps.minImageCount;
        }
        if (caps.maxImageCount != 0 && imageCount > caps.maxImageCount)
        {
            imageCount = caps.maxImageCount;
        }
        if (imageCount > MaxSwapchainImages)
        {
            imageCount = MaxSwapchainImages;
        }
        VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = state.surface;
        info.minImageCount = imageCount;
        info.imageFormat = wanted;
        info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = PickPresentMode(m_physicalDevice, state.surface, state.desc.presentMode);
        info.clipped = VK_TRUE;
        info.oldSwapchain = old;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        if (vk.vkCreateSwapchainKHR(m_device, &info, nullptr, &swapchain) != VK_SUCCESS)
        {
            return false;
        }
        if (old != VK_NULL_HANDLE)
        {
            vk.vkDestroySwapchainKHR(m_device, old, nullptr);
        }
        state.swapchain = swapchain;
        state.format = wanted;
        state.imageExtent = extent;
        std::uint32_t actualCount = 0;
        if (vk.vkGetSwapchainImagesKHR(m_device, swapchain, &actualCount, nullptr) != VK_SUCCESS
            || actualCount == 0 || actualCount > MaxSwapchainImages)
        {
            return false;
        }
        if (vk.vkGetSwapchainImagesKHR(m_device, swapchain, &actualCount, state.images) != VK_SUCCESS)
        {
            return false;
        }
        state.imageCount = actualCount;
        for (std::uint32_t index = 0; index < actualCount; ++index)
        {
            VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = state.images[index];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = wanted;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (vk.vkCreateImageView(m_device, &view, nullptr, &state.views[index]) != VK_SUCCESS
                || vk.vkCreateSemaphore(m_device, &semaphore, nullptr, &state.renderFinished[index]) != VK_SUCCESS)
            {
                return false;
            }
            state.layouts[index] = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        VkImageCreateInfo copy = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        copy.imageType = VK_IMAGE_TYPE_2D;
        copy.format = wanted;
        copy.extent = {state.desc.extent.width, state.desc.extent.height, 1};
        copy.mipLevels = 1;
        copy.arrayLayers = 1;
        copy.samples = VK_SAMPLE_COUNT_1_BIT;
        copy.tiling = VK_IMAGE_TILING_OPTIMAL;
        copy.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        copy.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        state.presentedCopyLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return CreateImage(copy, state.presentedCopy, state.presentedCopyMemory);
    }

    SwapchainHandle VulkanDevice::CreateSwapchain(const SwapchainDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_frameActive || desc.surface.value == 0
            || desc.extent.width == 0 || desc.extent.height == 0 || desc.bufferCount < 2
            || desc.maxFramesInFlight == 0 || ToVulkanFormat(desc.format) == VK_FORMAT_UNDEFINED
            || desc.format == TextureFormat::D32Float)
        {
            return {};
        }
        std::uint32_t index = MaxSwapchains;
        for (std::uint32_t at = 0; at < MaxSwapchains; ++at)
        {
            if (false == m_swapchains[at].occupied)
            {
                index = at;
                break;
            }
        }
        if (index == MaxSwapchains)
        {
            return {};
        }
        VulkanSwapchainState& state = m_swapchains[index];
        VkWin32SurfaceCreateInfoKHR surfaceInfo = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        surfaceInfo.hinstance = GetModuleHandleW(nullptr);
        surfaceInfo.hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(desc.surface.value));
        if (vk.vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &state.surface) != VK_SUCCESS)
        {
            state.surface = VK_NULL_HANDLE;
            return {};
        }
        VkBool32 presentable = VK_FALSE;
        if (vk.vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, state.surface, &presentable)
                != VK_SUCCESS
            || presentable == VK_FALSE)
        {
            vk.vkDestroySurfaceKHR(m_instance, state.surface, nullptr);
            state.surface = VK_NULL_HANDLE;
            return {};
        }
        state.desc = desc;
        if (false == BuildSwapchain(state, VK_NULL_HANDLE))
        {
            ReleaseSwapchainImages(state);
            if (state.swapchain != VK_NULL_HANDLE)
            {
                vk.vkDestroySwapchainKHR(m_device, state.swapchain, nullptr);
            }
            vk.vkDestroySurfaceKHR(m_instance, state.surface, nullptr);
            const std::uint32_t generation = state.generation;
            const std::uint32_t backBufferGeneration = state.backBufferGeneration;
            state = {};
            state.generation = generation;
            state.backBufferGeneration = backBufferGeneration;
            return {};
        }
        // 슬롯 수는 스왑체인의 요청을 따른다. 프레임 슬롯은 디바이스 것이라 스왑체인이 하나도 없을 때만 바꾼다 -
        // 부르는 쪽은 `GetFramesInFlight()` 로 슬롯 배열을 잡으므로 살아 있는 동안 바뀌면 안 된다. 바뀌면 지금
        // 슬롯 번호도 그 안으로 접는다.
        bool anotherLives = false;
        for (std::uint32_t at = 0; at < MaxSwapchains; ++at)
        {
            anotherLives = anotherLives || (at != index && m_swapchains[at].occupied);
        }
        if (false == anotherLives)
        {
            m_slotCount = desc.maxFramesInFlight > MaxFramesInFlight ? MaxFramesInFlight : desc.maxFramesInFlight;
            m_slot %= m_slotCount;
        }
        state.occupied = true;
        return SwapchainHandle{index, state.generation};
    }

    void VulkanDevice::DestroySwapchain(SwapchainHandle swapchain)
    {
        VulkanSwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr || m_frameActive)
        {
            return;
        }
        vk.vkDeviceWaitIdle(m_device);
        ReleaseSwapchainImages(*state);
        if (state->swapchain != VK_NULL_HANDLE)
        {
            vk.vkDestroySwapchainKHR(m_device, state->swapchain, nullptr);
        }
        if (state->surface != VK_NULL_HANDLE)
        {
            vk.vkDestroySurfaceKHR(m_instance, state->surface, nullptr);
        }
        const std::uint32_t generation = VulkanNextGeneration(state->generation);
        const std::uint32_t backBufferGeneration = VulkanNextGeneration(state->backBufferGeneration);
        *state = {};
        state->generation = generation;
        state->backBufferGeneration = backBufferGeneration;
    }

    bool VulkanDevice::ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent)
    {
        VulkanSwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr || m_frameActive || extent.width == 0 || extent.height == 0)
        {
            return false;
        }
        // 옛 이미지를 쓰는 프레임이 끝나야 한다. 크기 바꾸기는 드물어 여기서 기다린다.
        vk.vkDeviceWaitIdle(m_device);
        FlushRetired(~std::uint64_t{0});
        ReleaseSwapchainImages(*state);
        state->desc.extent = extent;
        const VkSwapchainKHR old = state->swapchain;
        state->swapchain = VK_NULL_HANDLE;
        if (false == BuildSwapchain(*state, old))
        {
            // 반쯤 선 스왑체인은 두지 않는다. 옛 것(만들기가 실패해 아직 살아 있으면)이든 새 것(뒷단계에서
            // 실패했으면)이든 지우고 비워 둔다 - 다음 `BeginFrame` 이 SurfaceLost 를 돌려 주고 호스트가 다시 시도한다.
            if (state->swapchain == VK_NULL_HANDLE)
            {
                state->swapchain = old;
            }
            ReleaseSwapchainImages(*state);
            if (state->swapchain != VK_NULL_HANDLE)
            {
                vk.vkDestroySwapchainKHR(m_device, state->swapchain, nullptr);
                state->swapchain = VK_NULL_HANDLE;
            }
            return false;
        }
        state->backBufferGeneration = VulkanNextGeneration(state->backBufferGeneration);
        return true;
    }

    // ── 프레임 ──────────────────────────────────────────────────────────────

    BeginFrameResult VulkanDevice::BeginFrame(SwapchainHandle swapchain)
    {
        BeginFrameResult result;
        if (m_status != FrameStatus::Ready || m_frameActive)
        {
            result.status = m_status == FrameStatus::Ready ? FrameStatus::InvalidState : m_status;
            return result;
        }
        VulkanSwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr)
        {
            result.status = FrameStatus::InvalidState;
            return result;
        }
        if (state->swapchain == VK_NULL_HANDLE)
        {
            // 크기 바꾸기가 실패해 비어 있다. 표면을 다시 세워야 한다.
            result.status = FrameStatus::SurfaceLost;
            return result;
        }
        VulkanFrameSlot& slot = m_slots[m_slot];
        // 이 슬롯의 지난 프레임이 끝나야 그 명령 버퍼와 풀을 다시 쓴다. 그 뒤로는 그 프레임까지의 은퇴 객체를 지울 수 있다.
        if (vk.vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, ~std::uint64_t{0}) != VK_SUCCESS)
        {
            MarkDeviceLost();
            result.status = FrameStatus::DeviceLost;
            return result;
        }
        if (slot.serial > m_completedSerial)
        {
            m_completedSerial = slot.serial;
        }
        FlushRetired(m_completedSerial);
        // 획득보다 먼저 비운다. 획득 뒤에 실패하면 신호가 걸린 세마포어를 기다릴 곳이 없어지므로, 실패할 수 있는 것은
        // 앞에 두고 획득 뒤에는 펜스 리셋만 남긴다(펜스는 획득 전에 리셋하면 실패 시 다음 프레임이 영원히 기다린다).
        if (vk.vkResetDescriptorPool(m_device, slot.descriptorPool, 0) != VK_SUCCESS
            || vk.vkResetCommandBuffer(slot.commands, 0) != VK_SUCCESS)
        {
            result.status = FrameStatus::InvalidState;
            return result;
        }
        VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vk.vkBeginCommandBuffer(slot.commands, &begin) != VK_SUCCESS)
        {
            result.status = FrameStatus::InvalidState;
            return result;
        }
        std::uint32_t imageIndex = 0;
        const VkResult acquired = vk.vkAcquireNextImageKHR(m_device, state->swapchain, ~std::uint64_t{0},
            slot.imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_DEVICE_LOST)
        {
            MarkDeviceLost();
            result.status = FrameStatus::DeviceLost;
            return result;
        }
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR || acquired == VK_ERROR_SURFACE_LOST_KHR)
        {
            result.status = FrameStatus::SurfaceLost;
            return result;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
        {
            result.status = FrameStatus::InvalidState;
            return result;
        }
        if (vk.vkResetFences(m_device, 1, &slot.fence) != VK_SUCCESS)
        {
            MarkDeviceLost();
            result.status = FrameStatus::DeviceLost;
            return result;
        }
        state->currentImage = imageIndex;
        // 제시된 이미지의 내용은 필요 없다. UNDEFINED 에서 시작하면 첫 전이가 내용을 버려도 된다.
        state->layouts[imageIndex] = VK_IMAGE_LAYOUT_UNDEFINED;
        m_activeSwapchainIndex = swapchain.index;
        m_frameActive = true;
        slot.serial = ++m_frameSerial;
        m_commandContext.BeginFrame(slot.commands, slot.descriptorPool);
        result.status = FrameStatus::Ready;
        result.frame.serial = m_frameSerial;
        result.frame.slot = m_slot;
        result.frame.backBuffer = TextureHandle{BackBufferTextureBase + swapchain.index, state->backBufferGeneration};
        result.frame.commands = &m_commandContext;
        return result;
    }

    FrameStatus VulkanDevice::SubmitAndPresent(bool present)
    {
        VulkanSwapchainState& state = m_swapchains[m_activeSwapchainIndex];
        VulkanFrameSlot& slot = m_slots[m_slot];
        m_frameActive = false;
        if (false == state.occupied || state.swapchain == VK_NULL_HANDLE)
        {
            return FrameStatus::InvalidState;
        }
        const std::uint32_t image = state.currentImage;
        if (present && state.presentedCopy != VK_NULL_HANDLE)
        {
            // 제시하면 이 이미지는 우리 것이 아니다. 되읽기용 사본을 먼저 뜬다 - 한 프레임에 복사 하나다.
            TransitionImage(slot.commands, state.images[image], VK_IMAGE_ASPECT_COLOR_BIT, state.layouts[image],
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            TransitionImage(slot.commands, state.presentedCopy, VK_IMAGE_ASPECT_COLOR_BIT, state.presentedCopyLayout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy region = {};
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.layerCount = 1;
            region.dstSubresource = region.srcSubresource;
            region.extent = {state.desc.extent.width, state.desc.extent.height, 1};
            vk.vkCmdCopyImage(slot.commands, state.images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                state.presentedCopy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            TransitionImage(slot.commands, state.presentedCopy, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            state.presentedCopyLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            state.layouts[image] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        TransitionImage(slot.commands, state.images[image], VK_IMAGE_ASPECT_COLOR_BIT, state.layouts[image],
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        state.layouts[image] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        if (vk.vkEndCommandBuffer(slot.commands) != VK_SUCCESS)
        {
            // 제출하지 못하면 이 슬롯의 펜스는 영원히 안 오고 얻어 온 이미지도 돌아가지 않는다. 계속 갈 길이 없다 -
            // 디바이스를 잃은 것으로 친다.
            MarkDeviceLost();
            return FrameStatus::DeviceLost;
        }
        VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = slot.imageAvailable;
        // 이미지가 준비될 때까지 기다릴 곳은 색 출력이다 - 그 앞의 정점 처리는 먼저 돌아도 된다. 첫 배리어의
        // 출발 스테이지가 ALL_COMMANDS 라 이 대기에 이어진다.
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = state.renderFinished[image];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo commands = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commands.commandBuffer = slot.commands;
        VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commands;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        const VkResult submitted = vk.vkQueueSubmit2(m_queue, 1, &submit, slot.fence);
        if (submitted == VK_ERROR_DEVICE_LOST)
        {
            MarkDeviceLost();
            return FrameStatus::DeviceLost;
        }
        if (submitted != VK_SUCCESS)
        {
            MarkDeviceLost();
            return FrameStatus::DeviceLost;
        }
        VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &state.renderFinished[image];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &state.swapchain;
        presentInfo.pImageIndices = &image;
        const VkResult presented = vk.vkQueuePresentKHR(m_queue, &presentInfo);
        m_slot = (m_slot + 1) % m_slotCount;
        if (presented == VK_ERROR_DEVICE_LOST)
        {
            MarkDeviceLost();
            return FrameStatus::DeviceLost;
        }
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_ERROR_SURFACE_LOST_KHR)
        {
            return FrameStatus::SurfaceLost;
        }
        return presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR ? FrameStatus::Ready
                                                                          : FrameStatus::InvalidState;
    }

    FrameStatus VulkanDevice::EndFrame(const FrameContext& frame)
    {
        if (false == m_frameActive || frame.serial != m_frameSerial)
        {
            return FrameStatus::InvalidState;
        }
        if (m_commandContext.IsRenderPassActive())
        {
            m_commandContext.EndRenderPass();
        }
        return SubmitAndPresent(true);
    }

    void VulkanDevice::AbortFrame(const FrameContext& frame)
    {
        if (false == m_frameActive || frame.serial != m_frameSerial)
        {
            return;
        }
        if (m_commandContext.IsRenderPassActive())
        {
            m_commandContext.EndRenderPass();
        }
        // 얻어 온 이미지는 돌려줘야 한다 - 안 돌려주면 다음 획득이 막힌다. 그린 것은 버리지만 이미지는
        // 그대로 제시한다(사본은 뜨지 않으니 되읽기는 지난 프레임을 본다). `[가정]`
        SubmitAndPresent(false);
    }

    FrameStatus VulkanDevice::GetStatus() const
    {
        return m_status;
    }

    void VulkanDevice::WaitIdle()
    {
        if (m_device == VK_NULL_HANDLE)
        {
            return;
        }
        vk.vkDeviceWaitIdle(m_device);
        // 기록 중인 프레임은 아직 제출되지 않았다. 그 프레임이 은퇴시킨 것은 그 명령 버퍼가 여전히 가리키므로 남긴다.
        m_completedSerial = m_frameActive ? m_frameSerial - 1 : m_frameSerial;
        FlushRetired(m_completedSerial);
    }

    std::uint32_t VulkanDevice::GetFramesInFlight() const
    {
        return m_slotCount;
    }

    std::uint32_t VulkanDevice::GetValidationErrorCount() const
    {
        return m_validationErrors;
    }

    // ── 되읽기 ──────────────────────────────────────────────────────────────

    bool VulkanDevice::ResolveReadableImage(TextureHandle texture, VkImage& image, VkImageLayout*& layout,
        TextureDesc& desc)
    {
        if (false == texture.IsValid() || texture.index < BackBufferTextureBase)
        {
            return false;
        }
        if (texture.index < TextureResourceBase)
        {
            const std::uint32_t index = texture.index - BackBufferTextureBase;
            if (index >= MaxSwapchains)
            {
                return false;
            }
            VulkanSwapchainState& state = m_swapchains[index];
            if (false == state.occupied || state.backBufferGeneration != texture.generation
                || state.presentedCopy == VK_NULL_HANDLE || state.presentedCopyLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                return false;
            }
            image = state.presentedCopy;
            layout = &state.presentedCopyLayout;
            desc = {};
            desc.extent = state.desc.extent;
            desc.format = state.desc.format;
            desc.usage = TextureUsage::RenderTarget;
            return true;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        VulkanTextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return false;
        }
        image = state.image;
        layout = &state.layout;
        desc = state.desc;
        return true;
    }

    bool VulkanDevice::ReadTexture(
        TextureHandle texture,
        std::byte* destination,
        std::size_t destinationSize,
        TextureReadback& result)
    {
        result = {};
        if (m_status != FrameStatus::Ready || m_device == VK_NULL_HANDLE || m_frameActive
            || destination == nullptr || destinationSize == 0)
        {
            return false;
        }
        VkImage image = VK_NULL_HANDLE;
        VkImageLayout* layout = nullptr;
        TextureDesc desc;
        if (false == ResolveReadableImage(texture, image, layout, desc) || image == VK_NULL_HANDLE)
        {
            return false;
        }
        const std::uint32_t pixelSize = VulkanPixelSize(desc.format);
        if (pixelSize == 0 || desc.format == TextureFormat::D32Float || desc.depthOrLayers != 1)
        {
            return false;
        }
        const std::size_t tightRowPitch = static_cast<std::size_t>(desc.extent.width) * pixelSize;
        const std::size_t requiredBytes = tightRowPitch * desc.extent.height;
        if (destinationSize < requiredBytes)
        {
            return false;
        }
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        if (false == CreateStagingBuffer(requiredBytes, staging, stagingMemory, mapped))
        {
            return false;
        }
        bool ok = BeginOneShot();
        if (ok)
        {
            // 아직 아무 레이아웃도 아니면(그린 적 없는 텍스처) 내용도 없다. 그래도 읽기는 성립한다 - 값이 무엇이든.
            // 되돌려 놓을 레이아웃. 아직 아무것도 아니던(UNDEFINED) 텍스처는 GENERAL 로 올려 둔다 - UNDEFINED 로는 돌아갈 수 없다.
            const VkImageLayout restoreTo = *layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_GENERAL : *layout;
            TransitionImage(m_oneShotCommands, image, VK_IMAGE_ASPECT_COLOR_BIT, *layout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {desc.extent.width, desc.extent.height, 1};
            vk.vkCmdCopyImageToBuffer(m_oneShotCommands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                &region);
            TransitionImage(m_oneShotCommands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                restoreTo);
            *layout = restoreTo;
            ok = EndOneShot();
        }
        if (ok)
        {
            std::memcpy(destination, mapped, requiredBytes);
            result.extent = desc.extent;
            result.format = desc.format;
            result.rowPitch = static_cast<std::uint32_t>(tightRowPitch);
            result.writtenBytes = static_cast<std::uint32_t>(requiredBytes);
        }
        vk.vkUnmapMemory(m_device, stagingMemory);
        vk.vkDestroyBuffer(m_device, staging, nullptr);
        vk.vkFreeMemory(m_device, stagingMemory, nullptr);
        return ok;
    }
}
