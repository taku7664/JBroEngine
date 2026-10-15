#include <JBro/RHI/ConstantRing.h>

namespace JBro
{
    ConstantRing::~ConstantRing()
    {
        Destroy();
    }

    Bool ConstantRing::Create(IRHIDevice& device, std::size_t bytesPerFrame)
    {
        if (m_device != nullptr || bytesPerFrame == 0)
        {
            return false;
        }
        const DeviceCaps caps = device.GetCaps();
        if (false == caps.constantBufferOffsets || caps.constantBufferAlignment == 0)
        {
            return false;
        }
        const UInt32 framesInFlight = device.GetFramesInFlight();
        if (framesInFlight == 0 || framesInFlight > MaxFrameSlots)
        {
            return false;
        }

        const std::size_t alignment = caps.constantBufferAlignment;
        BufferDesc desc;
        desc.size = (bytesPerFrame + alignment - 1) / alignment * alignment;
        desc.usage = BufferUsage::Constant;
        desc.memory = MemoryType::Upload;
        for (UInt32 slot = 0; slot < framesInFlight; ++slot)
        {
            m_buffers[slot] = device.CreateBuffer(desc);
            if (false == m_buffers[slot].IsValid())
            {
                for (UInt32 made = 0; made < slot; ++made)
                {
                    device.DestroyBuffer(m_buffers[made]);
                    m_buffers[made] = {};
                }
                return false;
            }
        }
        m_device = &device;
        m_capacity = desc.size;
        m_alignment = caps.constantBufferAlignment;
        m_maxRangeBytes = caps.maxConstantBufferBytes;
        m_slotCount = framesInFlight;
        m_slot = 0;
        m_cursor = 0;
        m_frameOpen = false;
        return true;
    }

    void ConstantRing::Destroy()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (UInt32 slot = 0; slot < m_slotCount; ++slot)
        {
            m_device->DestroyBuffer(m_buffers[slot]);
            m_buffers[slot] = {};
        }
        m_device = nullptr;
        m_capacity = 0;
        m_cursor = 0;
        m_slotCount = 0;
        m_frameOpen = false;
    }

    void ConstantRing::BeginFrame(UInt32 frameSlot)
    {
        if (m_device == nullptr)
        {
            return;
        }
        // 장치가 말한 슬롯 수 안으로 접는다. 스왑체인이 바뀌어 슬롯 수가 줄어도 남의 버퍼를 쓰지 않는다.
        m_slot = frameSlot % m_slotCount;
        m_cursor = 0;
        m_frameOpen = true;
    }

    ConstantRange ConstantRing::Push(JArrayView<std::byte> data)
    {
        if (false == m_frameOpen || data.size == 0 || data.data == nullptr || data.size > m_maxRangeBytes)
        {
            return {};
        }
        const std::size_t size = (static_cast<std::size_t>(data.size) + m_alignment - 1) / m_alignment * m_alignment;
        if (size > m_capacity - m_cursor)
        {
            return {};
        }
        const BufferHandle buffer = m_buffers[m_slot];
        if (false == m_device->WriteBuffer(buffer, m_cursor, data))
        {
            return {};
        }
        const ConstantRange range{buffer, m_cursor, size};
        m_cursor += size;
        return range;
    }
}
