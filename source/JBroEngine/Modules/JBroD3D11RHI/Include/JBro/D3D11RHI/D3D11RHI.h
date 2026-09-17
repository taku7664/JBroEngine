#pragma once

#include <JBro/RHI/RHI.h>

namespace JBro
{
    namespace Internal
    {
        class D3D11Device;
    }

    // D3D11 백엔드다(framework3d-plan 2단계, D-107). D3D12 와 같은 `IRHIDevice` 계약을 즉시 컨텍스트
    // 하나로 채운다 - 프레임 슬롯은 하나(`GetFramesInFlight() == 1`), 펜스도 디스크립터 힙도 없다.
    // 디버그 레이어는 디바이스 단위(`D3D11_CREATE_DEVICE_DEBUG`)라 프로세스 전체를 켜는 함수가 없다.
    // 셰이더는 DXBC(SM 5.0)만 받는다 - DXIL 은 D3D11 이 읽지 못한다.
    class D3D11RHIModule final : public IRHIModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        GraphicsApi GetApi() const override;
        IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) override;
        void DestroyDevice(IRHIDevice* device) override;

    private:
        Internal::D3D11Device* m_activeDevice = nullptr;
        bool m_initialized = false;
    };
}
