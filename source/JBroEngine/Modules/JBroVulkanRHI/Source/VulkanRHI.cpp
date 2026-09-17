#include <JBro/VulkanRHI/VulkanRHI.h>

#include "VulkanDevice.h"

#include <new>

namespace JBro
{
    bool VulkanRHIModule::Initialize(const JMemoryContext& memory)
    {
        static_cast<void>(memory);
        if (m_initialized || m_activeDevice != nullptr)
        {
            return false;
        }
        // 드라이버가 없는 기계다. 모듈부터 없다고 말한다 - 테스트는 이것을 보고 건너뛴다.
        if (false == Internal::LoadVulkanLibrary())
        {
            return false;
        }
        m_initialized = true;
        return true;
    }

    void VulkanRHIModule::Shutdown()
    {
        if (m_activeDevice != nullptr)
        {
            DestroyDevice(m_activeDevice);
        }
        m_initialized = false;
    }

    GraphicsApi VulkanRHIModule::GetApi() const
    {
        return GraphicsApi::Vulkan;
    }

    IRHIDevice* VulkanRHIModule::CreateDevice(const RHIDeviceCreateInfo& createInfo)
    {
        if (false == m_initialized || m_activeDevice != nullptr)
        {
            return nullptr;
        }
        Internal::VulkanDevice* device = new (std::nothrow) Internal::VulkanDevice();
        if (device == nullptr)
        {
            return nullptr;
        }
        if (false == device->Initialize(createInfo))
        {
            delete device;
            return nullptr;
        }
        m_activeDevice = device;
        return m_activeDevice;
    }

    void VulkanRHIModule::DestroyDevice(IRHIDevice* device)
    {
        if (device == nullptr || device != m_activeDevice)
        {
            return;
        }
        OwnerPtr<Internal::VulkanDevice> ownedDevice(m_activeDevice);
        m_activeDevice = nullptr;
        ownedDevice->Shutdown();
    }
}
