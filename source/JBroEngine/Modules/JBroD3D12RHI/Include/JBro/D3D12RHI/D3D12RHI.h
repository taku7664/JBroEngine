#pragma once

#include <JBro/RHI/RHI.h>

namespace JBro::Engine
{
    class D3D12Device;

    class D3D12RHIModule final : public IRHIModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        GraphicsApi GetApi() const override;
        IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) override;
        void DestroyDevice(IRHIDevice* device) override;

    private:
        D3D12Device* mActiveDevice = nullptr;
    };
}
