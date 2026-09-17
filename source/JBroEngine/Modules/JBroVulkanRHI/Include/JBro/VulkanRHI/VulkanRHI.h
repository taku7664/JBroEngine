#pragma once

#include <JBro/RHI/RHI.h>

namespace JBro
{
    namespace Internal
    {
        class VulkanDevice;
    }

    // Vulkan 백엔드다(framework3d-plan 3단계, D-108). D3D12 와 같은 `IRHIDevice` 계약을 Vulkan 1.3 의
    // 동적 렌더링(`vkCmdBeginRendering`)과 synchronization2 로 채운다. 렌더 패스·프레임버퍼 객체는 없다.
    //
    // `vulkan-1.dll` 은 실행 시간에 연다(`LoadLibrary`). 이 모듈을 링크하는 실행 파일은 Vulkan SDK 의
    // 가져오기 라이브러리가 필요 없고, 드라이버가 없는 기계에서는 `Initialize` 가 false 를 돌려준다 -
    // 테스트는 그것을 보고 건너뛴다.
    //
    // 셰이더는 SPIR-V 만 받는다. 푸시 상수는 b0 한 블록이고, 텍스처는 set 0 의 binding 8+, 샘플러는
    // binding 16+ 다(`dxc -spirv -fvk-t-shift 8 0 -fvk-s-shift 16 0`). 검증 레이어(`VK_LAYER_KHRONOS_validation`)는
    // 인스턴스 단위라 프로세스 전체를 켜는 함수가 없다 - `RHIDeviceCreateInfo::enableValidation` 이 켠다.
    class VulkanRHIModule final : public IRHIModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
        GraphicsApi GetApi() const override;
        IRHIDevice* CreateDevice(const RHIDeviceCreateInfo& createInfo) override;
        void DestroyDevice(IRHIDevice* device) override;

    private:
        Internal::VulkanDevice* m_activeDevice = nullptr;
        bool m_initialized = false;
    };
}
