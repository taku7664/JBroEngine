#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Platform.h>

namespace JBro
{
    enum class GraphicsApi : std::uint8_t
    {
        D3D12,
        Vulkan,
        WebGPU
    };

    struct BufferHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    struct TextureHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    struct RHIDeviceCreateInfo
    {
        SurfaceHandle surface;
        bool enableValidation = false;
    };

    class IRHIDevice
    {
    public:
        virtual ~IRHIDevice() = default;

        virtual BufferHandle CreateBuffer(std::size_t size) = 0;
        virtual TextureHandle CreateTexture(std::uint32_t width, std::uint32_t height) = 0;
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void WaitIdle() = 0;
    };

    class IRHIModule : public IModule
    {
    public:
        virtual GraphicsApi GetApi() const = 0;
        virtual IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) = 0;
        virtual void DestroyDevice(IRHIDevice* device) = 0;
    };
}
