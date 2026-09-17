#pragma once

// Vulkan 진입점은 실행 시간에 `vulkan-1.dll` 에서 가져온다(D-108). 프로토타입을 끄고 아래 표의 함수
// 포인터만 쓴다 - 링크 시간 의존이 없으니 이 모듈을 링크하는 실행 파일은 Vulkan SDK 를 몰라도 된다.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <vulkan/vulkan.h>

namespace JBro::Internal
{
    // 셋으로 나눠 싣는다: 인스턴스 없이 되는 것, 인스턴스가 있어야 되는 것, 디바이스가 있어야 되는 것.
#define JBRO_VULKAN_GLOBAL_FUNCTIONS(X)                                                                       \
    X(vkCreateInstance)                                                                                        \
    X(vkEnumerateInstanceExtensionProperties)                                                                  \
    X(vkEnumerateInstanceLayerProperties)                                                                      \
    X(vkEnumerateInstanceVersion)

#define JBRO_VULKAN_INSTANCE_FUNCTIONS(X)                                                                     \
    X(vkDestroyInstance)                                                                                       \
    X(vkEnumeratePhysicalDevices)                                                                              \
    X(vkGetPhysicalDeviceProperties)                                                                           \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                     \
    X(vkGetPhysicalDeviceFeatures2)                                                                            \
    X(vkEnumerateDeviceExtensionProperties)                                                                    \
    X(vkCreateDevice)                                                                                          \
    X(vkGetDeviceProcAddr)                                                                                     \
    X(vkDestroySurfaceKHR)                                                                                     \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                    \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                               \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                    \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)                                                               \
    X(vkCreateWin32SurfaceKHR)                                                                                 \
    X(vkGetPhysicalDeviceWin32PresentationSupportKHR)

    // 디버그 메신저는 확장이라 없을 수 있다. 따로 싣고 null 을 허용한다.
#define JBRO_VULKAN_DEBUG_FUNCTIONS(X)                                                                        \
    X(vkCreateDebugUtilsMessengerEXT)                                                                          \
    X(vkDestroyDebugUtilsMessengerEXT)

#define JBRO_VULKAN_DEVICE_FUNCTIONS(X)                                                                       \
    X(vkDestroyDevice)                                                                                         \
    X(vkGetDeviceQueue)                                                                                        \
    X(vkDeviceWaitIdle)                                                                                        \
    X(vkQueueSubmit2)                                                                                          \
    X(vkQueuePresentKHR)                                                                                       \
    X(vkCreateSwapchainKHR)                                                                                    \
    X(vkDestroySwapchainKHR)                                                                                   \
    X(vkGetSwapchainImagesKHR)                                                                                 \
    X(vkAcquireNextImageKHR)                                                                                   \
    X(vkCreateCommandPool)                                                                                     \
    X(vkDestroyCommandPool)                                                                                    \
    X(vkAllocateCommandBuffers)                                                                                \
    X(vkResetCommandBuffer)                                                                                    \
    X(vkBeginCommandBuffer)                                                                                    \
    X(vkEndCommandBuffer)                                                                                      \
    X(vkCreateFence)                                                                                           \
    X(vkDestroyFence)                                                                                          \
    X(vkWaitForFences)                                                                                         \
    X(vkResetFences)                                                                                           \
    X(vkCreateSemaphore)                                                                                       \
    X(vkDestroySemaphore)                                                                                      \
    X(vkCreateBuffer)                                                                                          \
    X(vkDestroyBuffer)                                                                                         \
    X(vkGetBufferMemoryRequirements)                                                                           \
    X(vkAllocateMemory)                                                                                        \
    X(vkFreeMemory)                                                                                            \
    X(vkBindBufferMemory)                                                                                      \
    X(vkMapMemory)                                                                                             \
    X(vkUnmapMemory)                                                                                           \
    X(vkCreateImage)                                                                                           \
    X(vkDestroyImage)                                                                                          \
    X(vkGetImageMemoryRequirements)                                                                            \
    X(vkBindImageMemory)                                                                                       \
    X(vkCreateImageView)                                                                                       \
    X(vkDestroyImageView)                                                                                      \
    X(vkCreateSampler)                                                                                         \
    X(vkDestroySampler)                                                                                        \
    X(vkCreateShaderModule)                                                                                    \
    X(vkDestroyShaderModule)                                                                                   \
    X(vkCreateDescriptorSetLayout)                                                                             \
    X(vkDestroyDescriptorSetLayout)                                                                            \
    X(vkCreatePipelineLayout)                                                                                  \
    X(vkDestroyPipelineLayout)                                                                                 \
    X(vkCreateGraphicsPipelines)                                                                               \
    X(vkDestroyPipeline)                                                                                       \
    X(vkCreateDescriptorPool)                                                                                  \
    X(vkDestroyDescriptorPool)                                                                                 \
    X(vkResetDescriptorPool)                                                                                   \
    X(vkAllocateDescriptorSets)                                                                                \
    X(vkUpdateDescriptorSets)                                                                                  \
    X(vkCmdBeginRendering)                                                                                     \
    X(vkCmdEndRendering)                                                                                       \
    X(vkCmdPipelineBarrier2)                                                                                   \
    X(vkCmdSetViewport)                                                                                        \
    X(vkCmdSetScissor)                                                                                         \
    X(vkCmdBindPipeline)                                                                                       \
    X(vkCmdBindVertexBuffers)                                                                                  \
    X(vkCmdBindIndexBuffer)                                                                                    \
    X(vkCmdPushConstants)                                                                                      \
    X(vkCmdBindDescriptorSets)                                                                                 \
    X(vkCmdDrawIndexed)                                                                                        \
    X(vkCmdCopyBufferToImage)                                                                                  \
    X(vkCmdCopyImageToBuffer)                                                                                  \
    X(vkCmdCopyImage)

    struct VulkanFunctions
    {
#define JBRO_VULKAN_DECLARE(name) PFN_##name name = nullptr;
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
        JBRO_VULKAN_GLOBAL_FUNCTIONS(JBRO_VULKAN_DECLARE)
        JBRO_VULKAN_INSTANCE_FUNCTIONS(JBRO_VULKAN_DECLARE)
        JBRO_VULKAN_DEBUG_FUNCTIONS(JBRO_VULKAN_DECLARE)
        JBRO_VULKAN_DEVICE_FUNCTIONS(JBRO_VULKAN_DECLARE)
#undef JBRO_VULKAN_DECLARE
    };

    // 프로세스에 하나다. 디바이스도 하나뿐이라(모듈이 그렇게 막는다) 표를 나눌 이유가 없다.
    extern VulkanFunctions vk;

    // `vulkan-1.dll` 을 열고 전역 함수를 싣는다. 실패하면 false 고 표는 비어 있다. 여러 번 불러도 된다.
    bool LoadVulkanLibrary();
    void UnloadVulkanLibrary();
    // 전부 실려야 true 다. 디버그 함수는 확장이 없으면 null 로 남고 그것은 실패가 아니다.
    bool LoadVulkanInstanceFunctions(VkInstance instance);
    bool LoadVulkanDeviceFunctions(VkDevice device);
}
