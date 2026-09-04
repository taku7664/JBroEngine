#include <JBro/D3D12RHI/D3D12RHI.h>

#include "D3D12Device.h"

namespace JBro
{
    bool D3D12RHIModule::Initialize(const JMemoryContext& memory)
    {
        static_cast<void>(memory);
        if (m_initialized || m_activeDevice != nullptr)
        {
            return false;
        }

        m_initialized = true;
        return true;
    }

    void D3D12RHIModule::Shutdown()
    {
        if (m_activeDevice != nullptr)
        {
            DestroyDevice(m_activeDevice);
        }
        m_initialized = false;
    }

    GraphicsApi D3D12RHIModule::GetApi() const
    {
        return GraphicsApi::D3D12;
    }

    IRHIDevice* D3D12RHIModule::CreateDevice(const RHIDeviceCreateInfo& createInfo)
    {
        if (false == m_initialized || m_activeDevice != nullptr)
        {
            return nullptr;
        }

        OwnerPtr<Internal::D3D12Device> device = MakeOwnerPtr<Internal::D3D12Device>();
        if (false == device->Initialize(createInfo))
        {
            return nullptr;
        }

        m_activeDevice = device.release();
        return m_activeDevice;
    }

    void D3D12RHIModule::DestroyDevice(IRHIDevice* device)
    {
        if (device == nullptr || device != m_activeDevice)
        {
            return;
        }

        OwnerPtr<Internal::D3D12Device> ownedDevice(m_activeDevice);
        m_activeDevice = nullptr;
        ownedDevice->Shutdown();
    }
}
