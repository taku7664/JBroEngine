#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    bool Renderer::Initialize(IRHIModule& rhi, const RendererConfig& config)
    {
        if (m_device != nullptr || rhi.GetApi() != config.api)
        {
            return false;
        }

        RHIDeviceCreateInfo createInfo;
        createInfo.surface = config.surface;
        createInfo.enableValidation = config.validation;

        IRHIDevice* device = rhi.CreateDevice(createInfo);
        if (device == nullptr)
        {
            return false;
        }

        m_rhi = &rhi;
        m_device = device;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (m_device != nullptr && m_rhi != nullptr)
        {
            m_rhi->DestroyDevice(m_device);
        }

        m_device = nullptr;
        m_rhi = nullptr;
    }

    void Renderer::BeginFrame()
    {
        if (m_device == nullptr)
        {
            return;
        }

        m_device->BeginFrame();
    }

    void Renderer::SetCamera(const CameraParams&)
    {
    }

    void Renderer::SubmitSprite(const SpriteSubmit&)
    {
    }

    void Renderer::SubmitMesh(const MeshSubmit&)
    {
    }

    void Renderer::EndFrame()
    {
        if (m_device == nullptr)
        {
            return;
        }

        m_device->EndFrame();
    }

    bool Renderer::IsDeviceLost() const
    {
        return false;
    }
}
