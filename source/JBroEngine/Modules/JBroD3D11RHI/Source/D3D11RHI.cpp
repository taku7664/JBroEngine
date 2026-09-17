#include <JBro/D3D11RHI/D3D11RHI.h>

#include "D3D11Device.h"

#include <new>

namespace JBro
{
    bool D3D11RHIModule::Initialize(const JMemoryContext& memory)
    {
        static_cast<void>(memory);
        if (m_initialized || m_activeDevice != nullptr)
        {
            return false;
        }
        m_initialized = true;
        return true;
    }

    void D3D11RHIModule::Shutdown()
    {
        if (m_activeDevice != nullptr)
        {
            DestroyDevice(m_activeDevice);
        }
        m_initialized = false;
    }

    GraphicsApi D3D11RHIModule::GetApi() const
    {
        return GraphicsApi::D3D11;
    }

    IRHIDevice* D3D11RHIModule::CreateDevice(const RHIDeviceCreateInfo& createInfo)
    {
        if (false == m_initialized || m_activeDevice != nullptr)
        {
            return nullptr;
        }
        Internal::D3D11Device* device = new (std::nothrow) Internal::D3D11Device();
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

    void D3D11RHIModule::DestroyDevice(IRHIDevice* device)
    {
        if (device == nullptr || device != m_activeDevice)
        {
            return;
        }
        OwnerPtr<Internal::D3D11Device> ownedDevice(m_activeDevice);
        m_activeDevice = nullptr;
        ownedDevice->Shutdown();
    }
}
