#include "VulkanLoader.h"

namespace JBro::Internal
{
    VulkanFunctions vk;

    namespace
    {
        HMODULE g_library = nullptr;
    }

    bool LoadVulkanLibrary()
    {
        if (g_library != nullptr)
        {
            return true;
        }
        g_library = LoadLibraryW(L"vulkan-1.dll");
        if (g_library == nullptr)
        {
            return false;
        }
        vk = {};
        vk.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(g_library, "vkGetInstanceProcAddr"));
        if (vk.vkGetInstanceProcAddr == nullptr)
        {
            UnloadVulkanLibrary();
            return false;
        }
        bool complete = true;
#define JBRO_VULKAN_LOAD(name)                                                                                 \
        vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(nullptr, #name));                     \
        complete = complete && vk.name != nullptr;
        JBRO_VULKAN_GLOBAL_FUNCTIONS(JBRO_VULKAN_LOAD)
#undef JBRO_VULKAN_LOAD
        if (false == complete)
        {
            UnloadVulkanLibrary();
            return false;
        }
        return true;
    }

    void UnloadVulkanLibrary()
    {
        if (g_library != nullptr)
        {
            FreeLibrary(g_library);
            g_library = nullptr;
        }
        vk = {};
    }

    bool LoadVulkanInstanceFunctions(VkInstance instance)
    {
        if (instance == VK_NULL_HANDLE || vk.vkGetInstanceProcAddr == nullptr)
        {
            return false;
        }
        bool complete = true;
#define JBRO_VULKAN_LOAD(name)                                                                                 \
        vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(instance, #name));                    \
        complete = complete && vk.name != nullptr;
        JBRO_VULKAN_INSTANCE_FUNCTIONS(JBRO_VULKAN_LOAD)
#undef JBRO_VULKAN_LOAD
#define JBRO_VULKAN_LOAD(name)                                                                                 \
        vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(instance, #name));
        JBRO_VULKAN_DEBUG_FUNCTIONS(JBRO_VULKAN_LOAD)
#undef JBRO_VULKAN_LOAD
        return complete;
    }

    bool LoadVulkanDeviceFunctions(VkDevice device)
    {
        if (device == VK_NULL_HANDLE || vk.vkGetDeviceProcAddr == nullptr)
        {
            return false;
        }
        bool complete = true;
#define JBRO_VULKAN_LOAD(name)                                                                                 \
        vk.name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(device, #name));                        \
        complete = complete && vk.name != nullptr;
        JBRO_VULKAN_DEVICE_FUNCTIONS(JBRO_VULKAN_LOAD)
#undef JBRO_VULKAN_LOAD
        return complete;
    }
}
