#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>
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
    // 실제 구현은 F1~G4 단계에서 채운다. 여기서는 시그니처만 확정한다.
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

        template <typename Fn>
        void ForEachLive(Fn&& function);

    private:
        struct Slot
        {
            alignas(T) std::byte storage[sizeof(T)];
            SafePtrDetail::ControlBlock* controlBlock = nullptr;
            std::uint32_t generation = 1;
            bool alive = false;
        };

        struct Chunk
        {
            Slot slots[ChunkSize];
        };

        static void IgnoreDelete(void*)
        {
        }

        static std::uint32_t NextGeneration(std::uint32_t generation)
        {
            ++generation;
            if (generation == 0)
            {
                generation = 1;
            }
            return generation;
        }

        Slot& GetSlot(std::size_t slotIndex)
        {
            return m_chunks[slotIndex / ChunkSize]->slots[slotIndex % ChunkSize];
        }

        const Slot& GetSlot(std::size_t slotIndex) const
        {
            return m_chunks[slotIndex / ChunkSize]->slots[slotIndex % ChunkSize];
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

        JAllocator m_allocator;
        Array<OwnerPtr<Chunk>> m_chunks;
        Array<std::uint32_t> m_freeSlots;
        std::size_t m_nextUnusedSlot = 0;
        std::size_t m_liveCount = 0;
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
            slot.controlBlock = new SafePtrDetail::ControlBlock(value, &IgnoreDelete);
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
        slot.generation = NextGeneration(slot.generation);
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
        m_chunks.Clear();
        m_nextUnusedSlot = 0;
        m_liveCount = 0;
    }

    template <typename T, std::size_t ChunkSize>
    bool TObjectPool<T, ChunkSize>::Reserve(std::size_t requestedCapacity)
    {
        const std::size_t requiredChunks =
            (requestedCapacity + ChunkSize - 1) / ChunkSize;
        try
        {
            m_chunks.Reserve(requiredChunks);
            m_freeSlots.Reserve(requiredChunks * ChunkSize);
            while (m_chunks.Size() < requiredChunks)
            {
                m_chunks.Add(MakeOwnerPtr<Chunk>());
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

    template <typename T, std::size_t ChunkSize>
    bool TObjectPool<T, ChunkSize>::FindSlot(T* value, std::size_t& outSlotIndex)
    {
        for (std::size_t slotIndex = 0; slotIndex < m_nextUnusedSlot; ++slotIndex)
        {
            Slot& slot = GetSlot(slotIndex);
            if (GetValue(slot) == value)
            {
                outSlotIndex = slotIndex;
                return true;
            }
        }
        return false;
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
                delete controlBlock;
            }
        }
    }
}
