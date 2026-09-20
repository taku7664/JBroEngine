#pragma once

#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace JBro::Network
{
    // 고정 크기 바이트 고리다. `Reset` 이 한 번 잡고 그 뒤로는 할당하지 않는다 -
    // 매 프레임 경로에 힙 할당을 두지 않는다는 규칙의 실행이다. 쓰기는 전부 아니면 전무다.
    class ByteRing
    {
    public:
        void Reset(std::uint32_t capacity)
        {
            m_storage.Resize(capacity);
            m_head = 0;
            m_size = 0;
        }

        std::uint32_t Capacity() const
        {
            return static_cast<std::uint32_t>(m_storage.Size());
        }

        std::uint32_t Size() const
        {
            return m_size;
        }

        std::uint32_t Free() const
        {
            return Capacity() - m_size;
        }

        bool IsEmpty() const
        {
            return 0 == m_size;
        }

        void Clear()
        {
            m_head = 0;
            m_size = 0;
        }

        bool Write(const void* data, std::uint32_t size)
        {
            if (size > Free())
            {
                return false;
            }
            const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
            const std::uint32_t capacity = Capacity();
            std::uint32_t tail = (m_head + m_size) % capacity;
            const std::uint32_t firstRun = (capacity - tail < size) ? (capacity - tail) : size;
            std::memcpy(m_storage.Data() + tail, bytes, firstRun);
            if (firstRun < size)
            {
                std::memcpy(m_storage.Data(), bytes + firstRun, size - firstRun);
            }
            m_size += size;
            return true;
        }

        std::uint32_t Peek(void* out, std::uint32_t max) const
        {
            const std::uint32_t count = (max < m_size) ? max : m_size;
            if (0 == count)
            {
                return 0;
            }
            std::uint8_t* bytes = static_cast<std::uint8_t*>(out);
            const std::uint32_t capacity = Capacity();
            const std::uint32_t firstRun = (capacity - m_head < count) ? (capacity - m_head) : count;
            std::memcpy(bytes, m_storage.Data() + m_head, firstRun);
            if (firstRun < count)
            {
                std::memcpy(bytes + firstRun, m_storage.Data(), count - firstRun);
            }
            return count;
        }

        // `offset` 만큼 건너뛴 곳부터 본다. 헤더 뒤의 페이로드를 복사 없이 확인할 때 쓴다.
        std::uint32_t PeekAt(std::uint32_t offset, void* out, std::uint32_t max) const
        {
            if (offset >= m_size)
            {
                return 0;
            }
            const std::uint32_t available = m_size - offset;
            const std::uint32_t count = (max < available) ? max : available;
            std::uint8_t* bytes = static_cast<std::uint8_t*>(out);
            const std::uint32_t capacity = Capacity();
            const std::uint32_t start = (m_head + offset) % capacity;
            const std::uint32_t firstRun = (capacity - start < count) ? (capacity - start) : count;
            std::memcpy(bytes, m_storage.Data() + start, firstRun);
            if (firstRun < count)
            {
                std::memcpy(bytes + firstRun, m_storage.Data(), count - firstRun);
            }
            return count;
        }

        void Discard(std::uint32_t count)
        {
            if (count >= m_size)
            {
                Clear();
                return;
            }
            m_head = (m_head + count) % Capacity();
            m_size -= count;
        }

        std::uint32_t Read(void* out, std::uint32_t max)
        {
            const std::uint32_t count = Peek(out, max);
            Discard(count);
            return count;
        }

        // 연속으로 읽을 수 있는 첫 구간이다. 소켓에 바로 넘길 때 복사를 아낀다.
        const std::uint8_t* ContiguousData(std::uint32_t& outSize) const
        {
            if (0 == m_size)
            {
                outSize = 0;
                return nullptr;
            }
            const std::uint32_t capacity = Capacity();
            outSize = (capacity - m_head < m_size) ? (capacity - m_head) : m_size;
            return m_storage.Data() + m_head;
        }

    private:
        Array<std::uint8_t> m_storage;
        std::uint32_t m_head = 0;
        std::uint32_t m_size = 0;
    };
}
