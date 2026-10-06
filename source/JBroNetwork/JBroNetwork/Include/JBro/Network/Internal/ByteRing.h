#pragma once

#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Network
{
    // 고정 크기 바이트 고리다. `Reset` 이 한 번 잡고 그 뒤로는 할당하지 않는다 -
    // 매 프레임 경로에 힙 할당을 두지 않는다는 규칙의 실행이다. 쓰기는 전부 아니면 전무다.
    class ByteRing
    {
    public:
        void Reset(UInt32 capacity)
        {
            m_storage.Resize(capacity);
            m_head = 0;
            m_size = 0;
        }

        UInt32 Capacity() const
        {
            return static_cast<std::uint32_t>(m_storage.Size());
        }

        UInt32 Size() const
        {
            return m_size;
        }

        UInt32 Free() const
        {
            return Capacity() - m_size;
        }

        Bool IsEmpty() const
        {
            return 0 == m_size;
        }

        void Clear()
        {
            m_head = 0;
            m_size = 0;
        }

        Bool Write(const void* data, UInt32 size)
        {
            if (size > Free())
            {
                return false;
            }
            const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
            const UInt32 capacity = Capacity();
            UInt32 tail = (m_head + m_size) % capacity;
            const UInt32 firstRun = (capacity - tail < size) ? (capacity - tail) : size;
            std::memcpy(m_storage.Data() + tail, bytes, firstRun);
            if (firstRun < size)
            {
                std::memcpy(m_storage.Data(), bytes + firstRun, size - firstRun);
            }
            m_size += size;
            return true;
        }

        UInt32 Peek(void* out, UInt32 max) const
        {
            const UInt32 count = (max < m_size) ? max : m_size;
            if (0 == count)
            {
                return 0;
            }
            std::uint8_t* bytes = static_cast<std::uint8_t*>(out);
            const UInt32 capacity = Capacity();
            const UInt32 firstRun = (capacity - m_head < count) ? (capacity - m_head) : count;
            std::memcpy(bytes, m_storage.Data() + m_head, firstRun);
            if (firstRun < count)
            {
                std::memcpy(bytes + firstRun, m_storage.Data(), count - firstRun);
            }
            return count;
        }

        // `offset` 만큼 건너뛴 곳부터 본다. 헤더 뒤의 페이로드를 복사 없이 확인할 때 쓴다.
        UInt32 PeekAt(UInt32 offset, void* out, UInt32 max) const
        {
            if (offset >= m_size)
            {
                return 0;
            }
            const UInt32 available = m_size - offset;
            const UInt32 count = (max < available) ? max : available;
            std::uint8_t* bytes = static_cast<std::uint8_t*>(out);
            const UInt32 capacity = Capacity();
            const UInt32 start = (m_head + offset) % capacity;
            const UInt32 firstRun = (capacity - start < count) ? (capacity - start) : count;
            std::memcpy(bytes, m_storage.Data() + start, firstRun);
            if (firstRun < count)
            {
                std::memcpy(bytes + firstRun, m_storage.Data(), count - firstRun);
            }
            return count;
        }

        void Discard(UInt32 count)
        {
            if (count >= m_size)
            {
                Clear();
                return;
            }
            m_head = (m_head + count) % Capacity();
            m_size -= count;
        }

        UInt32 Read(void* out, UInt32 max)
        {
            const UInt32 count = Peek(out, max);
            Discard(count);
            return count;
        }

        // 연속으로 읽을 수 있는 첫 구간이다. 소켓에 바로 넘길 때 복사를 아낀다.
        const std::uint8_t* ContiguousData(UInt32& outSize) const
        {
            if (0 == m_size)
            {
                outSize = 0;
                return nullptr;
            }
            const UInt32 capacity = Capacity();
            outSize = (capacity - m_head < m_size) ? (capacity - m_head) : m_size;
            return m_storage.Data() + m_head;
        }

    private:
        Array<std::uint8_t> m_storage;
        UInt32 m_head = 0;
        UInt32 m_size = 0;
    };
}
