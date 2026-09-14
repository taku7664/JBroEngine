#pragma once

#include <JBro/RHI/RHI.h>

namespace JBro
{
    namespace Internal
    {
        class D3D12Device;
    }

    // D3D12 의 디버그 레이어를 이 프로세스에서 켠다.
    //
    // **첫 디바이스를 만들기 전에 불러야 한다.** 레이어는 프로세스 단위이고,
    // 디바이스가 하나라도 생긴 뒤에는 켜도 D3D12 가 무시한다. 그래도 아무 말도
    // 하지 않으므로, 늦게 부르면 "검증을 켰는데 조용하다" 는 잘못된 안심만 남는다.
    //
    // false 는 이 기계에 디버그 레이어가 없다는 뜻이다(Windows 의 그래픽 도구 기능).
    bool EnableD3D12ValidationForProcess();

    class D3D12RHIModule final : public IRHIModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        GraphicsApi GetApi() const override;
        IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) override;
        void DestroyDevice(IRHIDevice* device) override;

    private:
        Internal::D3D12Device* m_activeDevice = nullptr;
        bool m_initialized = false;
    };
}
