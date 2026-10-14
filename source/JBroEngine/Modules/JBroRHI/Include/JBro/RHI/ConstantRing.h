#pragma once

#include <JBro/RHI/RHI.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // `SetConstantBuffer` 에 그대로 넘기는 구간이다. 비어 있으면(`buffer` 가 무효) 링이 찼거나 열리지 않은 것이다.
    struct ConstantRange
    {
        BufferHandle buffer;
        std::size_t offset = 0;
        std::size_t size = 0;

        constexpr Bool IsValid() const noexcept
        {
            return buffer.IsValid();
        }
    };

    // 프레임마다 바뀌는 상수를 올리는 링이다(postprocess-plan §2.1). 프레임 슬롯마다 `Upload` 상수 버퍼 하나를 두고,
    // 프레임 안에서는 정렬된 다음 자리에 memcpy 만 한다 - **프레임 경로에 할당이 없다.**
    //
    // 슬롯을 나누는 까닭은 `GetFramesInFlight` 의 주석과 같다. 그 슬롯의 지난 프레임이 끝났다는 것은 `BeginFrame` 이
    // 보장하므로, 슬롯의 버퍼를 처음부터 다시 써도 GPU 가 읽는 중인 자리를 덮지 않는다. 한 슬롯만 주는 백엔드(D3D11)는
    // 오프셋 0 쓰기에서 버퍼를 갈아 끼운다(`IRHIDevice::WriteBuffer` 의 계약).
    //
    // 메인 스레드 전용이다. 장치보다 먼저 `Destroy` 한다.
    class ConstantRing
    {
    public:
        static constexpr UInt32 MaxFrameSlots = 3;

        ConstantRing() = default;
        ConstantRing(const ConstantRing&) = delete;
        ConstantRing& operator=(const ConstantRing&) = delete;
        ~ConstantRing();

        // 슬롯마다 `bytesPerFrame` 을 정렬 단위로 올린 크기의 버퍼를 만든다. 프레임 밖에서 부른다.
        // 장치가 오프셋 묶기를 못 하거나(`DeviceCaps::constantBufferOffsets`) 버퍼를 못 만들면 거짓이고 아무것도 남기지 않는다.
        Bool Create(IRHIDevice& device, std::size_t bytesPerFrame);
        void Destroy();
        Bool IsCreated() const noexcept
        {
            return m_device != nullptr;
        }

        // 그 프레임 슬롯(`FrameContext::slot`)의 버퍼를 처음부터 쓴다. 프레임마다 처음에 한 번 부른다.
        void BeginFrame(UInt32 frameSlot);
        // 다음 정렬된 자리에 쓰고 묶을 구간을 준다. 크기는 정렬 단위로 올린다 - 남는 꼬리는 셰이더가 읽지 않는다.
        // 링이 찼거나 `BeginFrame` 전이거나 한 번에 묶을 수 있는 크기를 넘으면 빈 구간이다.
        ConstantRange Push(JArrayView<std::byte> data);

        template <typename T>
        ConstantRange Push(const T& value)
        {
            return Push(JArrayView<std::byte>{reinterpret_cast<const std::byte*>(&value), static_cast<UInt32>(sizeof(T))});
        }

        // 이 프레임에 쓴 바이트 수다(정렬로 올린 꼬리 포함). 링의 크기를 정할 때 본다.
        std::size_t GetUsedBytes() const noexcept
        {
            return m_cursor;
        }
        std::size_t GetCapacity() const noexcept
        {
            return m_capacity;
        }

    private:
        IRHIDevice* m_device = nullptr;
        BufferHandle m_buffers[MaxFrameSlots];
        std::size_t m_capacity = 0;
        std::size_t m_cursor = 0;
        UInt32 m_alignment = 0;
        UInt32 m_maxRangeBytes = 0;
        UInt32 m_slotCount = 0;
        UInt32 m_slot = 0;
        Bool m_frameOpen = false;
    };
}
