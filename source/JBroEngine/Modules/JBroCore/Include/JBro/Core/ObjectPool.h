#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace JBro
{
    // 청크 기반 오브젝트 풀. 요구:
    //   1) 한 번 발급된 T* 는 다른 T 가 추가돼도 이동하지 않는다(주소 안정성).
    //   2) 파괴는 슬롯을 free-list 로 되돌리고 다음 Create 가 재사용한다.
    //   3) ForEachLive 는 살아 있는 원소만 순회한다(밀집 순회는 아니지만 스킵 저렴).
    // 청크 메모리는 생성자에서 받은 JAllocator로 할당하고 같은 allocator로 반환한다.
    template <typename T, std::size_t ChunkSize = 32>
    class TObjectPool
    {
        static_assert(ChunkSize > 0, "object pool chunks cannot be empty");

    public:
        explicit TObjectPool(JAllocator allocator) : m_allocator(allocator) {}
        ~TObjectPool()
        {
            Clear();
        }

        TObjectPool(const TObjectPool&)            = delete;
        TObjectPool& operator=(const TObjectPool&) = delete;

        template <typename... Args>
        T*     Create(Args&&... args);
        bool   Destroy(T* value);
        void   Clear();
        bool   Reserve(std::size_t requestedCapacity);

        std::size_t GetCapacity()  const;
        std::size_t GetLiveCount() const;

        // 진단용. §9 의 "정상 프레임에 힙 할당 0회" 를 테스트가 문장이 아니라 수로 확인한다.
        // ControlBlock 은 SafePtr 계약상 new/delete 로 살아야 해서 JAllocator 를 타지 않는다.
        // 그래서 청크 할당만 세는 카운팅 할당기로는 보이지 않고, 이 카운터가 그 자리를 메운다.
        std::size_t GetControlBlockAllocationCount() const { return m_controlBlockAllocations; }
        // Destroy 가 슬롯을 찾을 때 밟은 이분 탐색 단계 수의 누적. 전 슬롯 선형 탐색으로
        // 되돌아가면 이 값이 살아 있는 객체 수에 비례해 늘어난다.
        std::size_t GetSlotSearchStepCount() const { return m_slotSearchSteps; }

        template <typename Fn>
        void ForEachLive(Fn&& function);

    private:
        // 세대는 InstanceRegistry 가 단독으로 관리한다. 슬롯은 점유 여부만 안다.
        struct Slot
        {
            alignas(T) std::byte storage[sizeof(T)];
            SafePtrDetail::ControlBlock* controlBlock = nullptr;
            bool alive = false;
        };

        struct Chunk
        {
            Slot slots[ChunkSize];
        };

        class ChunkOwner final
        {
        public:
            ChunkOwner() = default;

            ChunkOwner(Chunk* chunk, JAllocator allocator)
                : m_chunk(chunk)
                , m_allocator(allocator)
            {
            }

            ChunkOwner(const ChunkOwner&) = delete;
            ChunkOwner& operator=(const ChunkOwner&) = delete;

            ChunkOwner(ChunkOwner&& other) noexcept
                : m_chunk(other.m_chunk)
                , m_allocator(other.m_allocator)
            {
                other.m_chunk = nullptr;
                other.m_allocator = {};
            }

            ChunkOwner& operator=(ChunkOwner&& other) noexcept
            {
                if (this == &other)
                {
                    return *this;
                }

                Release();
                m_chunk = other.m_chunk;
                m_allocator = other.m_allocator;
                other.m_chunk = nullptr;
                other.m_allocator = {};
                return *this;
            }

            ~ChunkOwner()
            {
                Release();
            }

            Chunk* Get() const
            {
                return m_chunk;
            }

        private:
            void Release()
            {
                if (m_chunk == nullptr)
                {
                    return;
                }

                std::destroy_at(m_chunk);
                m_allocator.free(m_allocator.userData, m_chunk);
                m_chunk = nullptr;
                m_allocator = {};
            }

            Chunk* m_chunk = nullptr;
            JAllocator m_allocator;
        };

        // 청크 하나가 차지하는 주소 구간. Destroy 가 T* 하나로 슬롯을 찾는 데 쓴다.
        // 청크는 Clear 전까지 이동·해제되지 않으므로 이 색인은 청크 할당 때만 갱신된다.
        struct ChunkBound
        {
            const std::byte* base = nullptr;
            std::size_t      chunkIndex = 0;
        };

        static void IgnoreDelete(void*)
        {
        }

        // 재사용 대기 블록이 있으면 되살리고 없으면 새로 잡는다. 대기열에는 참조가 0 인 블록만
        // 들어오므로(DestroySlot 참조) 살아 있는 SafePtr 를 다음 객체로 되살리는 사고는 없다.
        SafePtrDetail::ControlBlock* AcquireControlBlock(T* value)
        {
            if (false == m_freeBlocks.IsEmpty())
            {
                SafePtrDetail::ControlBlock* block = m_freeBlocks.Last();
                m_freeBlocks.RemoveAt(m_freeBlocks.Size() - 1);
                block->Ptr = static_cast<void*>(value);
                block->Alive = true;
                block->SafeCount = 0;
                block->Deleter = &IgnoreDelete;
                return block;
            }
            ++m_controlBlockAllocations;
            return new SafePtrDetail::ControlBlock(value, &IgnoreDelete);
        }

        void ReleaseCachedControlBlocks()
        {
            for (SafePtrDetail::ControlBlock* block : m_freeBlocks)
            {
                delete block;
            }
            m_freeBlocks.Clear();
        }

        Slot& GetSlot(std::size_t slotIndex)
        {
            return m_chunks[slotIndex / ChunkSize].Get()->slots[slotIndex % ChunkSize];
        }

        const Slot& GetSlot(std::size_t slotIndex) const
        {
            return m_chunks[slotIndex / ChunkSize].Get()->slots[slotIndex % ChunkSize];
        }

        static T* GetValue(Slot& slot)
        {
            return std::launder(reinterpret_cast<T*>(slot.storage));
        }

        static const T* GetValue(const Slot& slot)
        {
            return std::launder(reinterpret_cast<const T*>(slot.storage));
        }

        bool FindSlot(T* value, std::size_t& outSlotIndex);
        void DestroySlot(Slot& slot);
        ChunkOwner AllocateChunk();

        JAllocator m_allocator;
        Array<ChunkOwner> m_chunks;
        Array<ChunkBound> m_chunkBounds;
        Array<SafePtrDetail::ControlBlock*> m_freeBlocks;
        Array<std::uint32_t> m_freeSlots;
        std::size_t m_nextUnusedSlot = 0;
        std::size_t m_liveCount = 0;
        std::size_t m_controlBlockAllocations = 0;
        std::size_t m_slotSearchSteps = 0;
    };

    template <typename T, std::size_t ChunkSize>
    template <typename... Args>
    T* TObjectPool<T, ChunkSize>::Create(Args&&... args)
    {
        std::size_t slotIndex = 0;
        bool reusedSlot = false;
        if (false == m_freeSlots.IsEmpty())
        {
            slotIndex = m_freeSlots.Last();
            m_freeSlots.RemoveAt(m_freeSlots.Size() - 1);
            reusedSlot = true;
        }
        else
        {
            if (m_nextUnusedSlot == GetCapacity()
                && false == Reserve(m_nextUnusedSlot + 1))
            {
                return nullptr;
            }
            slotIndex = m_nextUnusedSlot++;
        }

        Slot& slot = GetSlot(slotIndex);
        T* value = nullptr;
        try
        {
            T* storage = reinterpret_cast<T*>(slot.storage);
            value = std::construct_at(storage, std::forward<Args>(args)...);
            slot.controlBlock = AcquireControlBlock(value);
            SafePtrDetail::BindSafeFromThisControlBlock(value, slot.controlBlock);
        }
        catch (...)
        {
            if (value != nullptr)
            {
                std::destroy_at(value);
            }
            if (reusedSlot)
            {
                m_freeSlots.Add(static_cast<std::uint32_t>(slotIndex));
            }
            else
            {
                --m_nextUnusedSlot;
            }
            throw;
        }

        slot.alive = true;
        ++m_liveCount;
        return value;
    }

    template <typename T, std::size_t ChunkSize>
    bool TObjectPool<T, ChunkSize>::Destroy(T* value)
    {
        std::size_t slotIndex = 0;
        if (value == nullptr || false == FindSlot(value, slotIndex))
        {
            return false;
        }

        Slot& slot = GetSlot(slotIndex);
        if (false == slot.alive)
        {
            return false;
        }

        DestroySlot(slot);
        m_freeSlots.Add(static_cast<std::uint32_t>(slotIndex));
        --m_liveCount;
        return true;
    }

    template <typename T, std::size_t ChunkSize>
    void TObjectPool<T, ChunkSize>::Clear()
    {
        for (std::size_t slotIndex = 0; slotIndex < m_nextUnusedSlot; ++slotIndex)
        {
            Slot& slot = GetSlot(slotIndex);
            if (slot.alive)
            {
                DestroySlot(slot);
            }
        }

        m_freeSlots.Clear();
        ReleaseCachedControlBlocks();
        m_chunkBounds.Clear();
        m_chunks.Clear();
        m_nextUnusedSlot = 0;
        m_liveCount = 0;
    }

    template <typename T, std::size_t ChunkSize>
    bool TObjectPool<T, ChunkSize>::Reserve(std::size_t requestedCapacity)
    {
        std::size_t requiredChunks = requestedCapacity / ChunkSize;
        if (requestedCapacity % ChunkSize != 0)
        {
            ++requiredChunks;
        }
        if (requiredChunks > std::numeric_limits<std::size_t>::max() / ChunkSize)
        {
            return false;
        }
        try
        {
            m_chunks.Reserve(requiredChunks);
            m_freeSlots.Reserve(requiredChunks * ChunkSize);
            m_chunkBounds.Reserve(requiredChunks);
            while (m_chunks.Size() < requiredChunks)
            {
                ChunkOwner chunk = AllocateChunk();
                if (chunk.Get() == nullptr)
                {
                    return false;
                }
                const std::byte* base =
                    reinterpret_cast<const std::byte*>(&chunk.Get()->slots[0]);
                const std::size_t chunkIndex = m_chunks.Size();
                m_chunks.Add(std::move(chunk));

                // 주소 순으로 끼워 넣는다. 할당기가 주는 주소는 순서가 보장되지 않는다.
                std::size_t position = m_chunkBounds.Size();
                while (position > 0
                    && std::less<const std::byte*>{}(base, m_chunkBounds[position - 1].base))
                {
                    --position;
                }
                m_chunkBounds.Insert(position, ChunkBound{base, chunkIndex});
            }

            // 스폰이 힙을 건드리지 않도록 ControlBlock 도 미리 확보한다(§9).
            m_freeBlocks.Reserve(requestedCapacity);
            while (m_freeBlocks.Size() + m_liveCount < requestedCapacity)
            {
                ++m_controlBlockAllocations;
                m_freeBlocks.Add(new SafePtrDetail::ControlBlock(nullptr, &IgnoreDelete));
            }
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    template <typename T, std::size_t ChunkSize>
    std::size_t TObjectPool<T, ChunkSize>::GetCapacity() const
    {
        return m_chunks.Size() * ChunkSize;
    }

    template <typename T, std::size_t ChunkSize>
    std::size_t TObjectPool<T, ChunkSize>::GetLiveCount() const
    {
        return m_liveCount;
    }

    template <typename T, std::size_t ChunkSize>
    template <typename Fn>
    void TObjectPool<T, ChunkSize>::ForEachLive(Fn&& function)
    {
        for (std::size_t slotIndex = 0; slotIndex < m_nextUnusedSlot; ++slotIndex)
        {
            Slot& slot = GetSlot(slotIndex);
            if (slot.alive)
            {
                function(*GetValue(slot));
            }
        }
    }

    // 청크 베이스 주소로 이분 탐색한 뒤 포인터 차로 슬롯을 계산한다. 전 슬롯 선형 탐색이면
    // 오브젝트 하나를 파괴할 때마다 살아 있는 전체를 훑게 된다(§9).
    template <typename T, std::size_t ChunkSize>
    bool TObjectPool<T, ChunkSize>::FindSlot(T* value, std::size_t& outSlotIndex)
    {
        if (value == nullptr || m_chunkBounds.IsEmpty())
        {
            return false;
        }

        const std::byte* address = reinterpret_cast<const std::byte*>(value);
        std::size_t low = 0;
        std::size_t high = m_chunkBounds.Size();
        while (low < high)
        {
            ++m_slotSearchSteps;
            const std::size_t middle = low + (high - low) / 2;
            // 서로 다른 할당에서 온 포인터의 < 비교는 표준상 미지정이다.
            // std::less 계열만 전순서를 보장하므로 그것을 쓴다.
            if (false == std::less<const std::byte*>{}(address, m_chunkBounds[middle].base))
            {
                low = middle + 1;
                continue;
            }
            high = middle;
        }
        if (low == 0)
        {
            return false;
        }

        const ChunkBound& bound = m_chunkBounds[low - 1];
        const std::size_t offset = static_cast<std::size_t>(address - bound.base);
        if (offset >= sizeof(Chunk) || offset % sizeof(Slot) != 0)
        {
            return false;
        }

        const std::size_t slotInChunk = offset / sizeof(Slot);
        const std::size_t slotIndex = bound.chunkIndex * ChunkSize + slotInChunk;
        if (slotIndex >= m_nextUnusedSlot || GetValue(GetSlot(slotIndex)) != value)
        {
            return false;
        }
        outSlotIndex = slotIndex;
        return true;
    }

    template <typename T, std::size_t ChunkSize>
    void TObjectPool<T, ChunkSize>::DestroySlot(Slot& slot)
    {
        std::destroy_at(GetValue(slot));
        SafePtrDetail::ControlBlock* controlBlock = slot.controlBlock;
        slot.controlBlock = nullptr;
        slot.alive = false;
        if (controlBlock != nullptr)
        {
            controlBlock->Ptr = nullptr;
            controlBlock->Alive = false;
            if (controlBlock->SafeCount == 0)
            {
                // 이 블록을 보는 SafePtr 가 없다. 힙에 돌려주지 않고 다음 Create 가 되살린다.
                //
                // 이 함수는 Clear 를 통해 소멸자에서도 불린다. Array::Add 는 재할당 시
                // 던질 수 있으므로 용량이 남아 있을 때만 담고, 아니면 그냥 돌려준다.
                // Reserve 가 슬롯 수만큼 용량을 잡아 두므로 정상 경로에서는 항상 담긴다.
                if (m_freeBlocks.Size() < m_freeBlocks.Capacity())
                {
                    m_freeBlocks.Add(controlBlock);
                }
                else
                {
                    delete controlBlock;
                }
            }
            // 참조가 남아 있으면 블록은 풀을 떠난다. 만료 판정에 계속 쓰이다가 마지막
            // SafePtr::ReleaseRef 가 지운다. 여기서 대기열에 담으면 산 참조가 되살아난다.
        }
    }

    template <typename T, std::size_t ChunkSize>
    typename TObjectPool<T, ChunkSize>::ChunkOwner
    TObjectPool<T, ChunkSize>::AllocateChunk()
    {
        if (m_allocator.allocate == nullptr || m_allocator.free == nullptr)
        {
            return {};
        }

        void* memory = m_allocator.allocate(
            m_allocator.userData,
            sizeof(Chunk),
            alignof(Chunk));
        if (memory == nullptr)
        {
            return {};
        }

        try
        {
            return ChunkOwner(std::construct_at(static_cast<Chunk*>(memory)), m_allocator);
        }
        catch (...)
        {
            m_allocator.free(m_allocator.userData, memory);
            throw;
        }
    }
}
