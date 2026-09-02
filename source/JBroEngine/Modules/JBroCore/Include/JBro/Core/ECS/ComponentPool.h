#pragma once

#include <JBro/Core/Core.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace JBro::Engine
{
    class IComponentPool
    {
    public:
        virtual ~IComponentPool() = default;

        virtual void Clear() = 0;
        virtual std::size_t GetLiveCount() const = 0;
        virtual std::size_t GetCapacity() const = 0;
        virtual std::size_t GetComponentSize() const = 0;
        virtual std::size_t GetComponentAlignment() const = 0;
    };

    template <typename T, std::size_t ComponentsPerChunk = 64>
    class TComponentPool final : public IComponentPool
    {
        static_assert(ComponentsPerChunk > 0);

    public:
        explicit TComponentPool(JAllocator allocator)
            : m_allocator(allocator)
        {
        }

        ~TComponentPool() override
        {
            Clear();
            ReleaseMemory();
        }

        TComponentPool(const TComponentPool&) = delete;
        TComponentPool& operator=(const TComponentPool&) = delete;
        TComponentPool(TComponentPool&&) = delete;
        TComponentPool& operator=(TComponentPool&&) = delete;

        template <typename... Args>
        T* Create(Args&&... args)
        {
            if (false == HasAllocator() || false == EnsureLiveCapacity(m_liveCount + 1))
            {
                return nullptr;
            }

            Slot* slot = AcquireSlot();
            if (slot == nullptr)
            {
                return nullptr;
            }

            T* component = nullptr;
            try
            {
                component = std::construct_at(
                    reinterpret_cast<T*>(slot->storage),
                    std::forward<Args>(args)...);
            }
            catch (...)
            {
                ReleaseSlot(*slot);
                throw;
            }

            slot->occupied = true;
            slot->liveIndex = m_liveCount;
            m_liveComponents[m_liveCount] = component;
            ++m_liveCount;
            return component;
        }

        bool Destroy(T* component)
        {
            if (component == nullptr)
            {
                return false;
            }

            Slot& slot = *reinterpret_cast<Slot*>(component);
            if (false == slot.occupied)
            {
                return false;
            }

            RemoveLiveComponent(slot);
            std::destroy_at(component);
            ReleaseSlot(slot);
            return true;
        }

        void Clear() override
        {
            while (m_liveCount > 0)
            {
                Destroy(m_liveComponents[m_liveCount - 1]);
            }
        }

        bool Reserve(std::size_t capacity)
        {
            if (false == HasAllocator() || false == EnsureLiveCapacity(capacity))
            {
                return false;
            }

            while (GetCapacity() < capacity)
            {
                if (false == AddChunk())
                {
                    return false;
                }
            }
            return true;
        }

        template <typename Fn>
        void ForEachLive(Fn&& function)
        {
            for (std::size_t index = 0; index < m_liveCount; ++index)
            {
                function(*m_liveComponents[index]);
            }
        }

        template <typename Fn>
        void ForEachLive(Fn&& function) const
        {
            for (std::size_t index = 0; index < m_liveCount; ++index)
            {
                function(*m_liveComponents[index]);
            }
        }

        std::size_t GetLiveCount() const override
        {
            return m_liveCount;
        }

        std::size_t GetCapacity() const override
        {
            return m_chunkCount * ComponentsPerChunk;
        }

        std::size_t GetComponentSize() const override
        {
            return sizeof(T);
        }

        std::size_t GetComponentAlignment() const override
        {
            return alignof(T);
        }

    private:
        static constexpr std::size_t InvalidLiveIndex = static_cast<std::size_t>(-1);

        struct Slot
        {
            alignas(T) unsigned char storage[sizeof(T)];
            Slot* nextFree = nullptr;
            std::size_t liveIndex = InvalidLiveIndex;
            bool occupied = false;
        };

        struct Chunk
        {
            Slot slots[ComponentsPerChunk];
            Chunk* next = nullptr;
        };

        bool HasAllocator() const
        {
            return m_allocator.allocate != nullptr && m_allocator.free != nullptr;
        }

        Slot* AcquireSlot()
        {
            if (m_freeHead == nullptr && false == AddChunk())
            {
                return nullptr;
            }

            Slot* slot = m_freeHead;
            m_freeHead = slot->nextFree;
            slot->nextFree = nullptr;
            return slot;
        }

        void ReleaseSlot(Slot& slot)
        {
            slot.occupied = false;
            slot.liveIndex = InvalidLiveIndex;
            slot.nextFree = m_freeHead;
            m_freeHead = &slot;
        }

        bool AddChunk()
        {
            void* memory = m_allocator.allocate(
                m_allocator.userData,
                sizeof(Chunk),
                std::max(alignof(Chunk), alignof(void*)));
            if (memory == nullptr)
            {
                return false;
            }

            Chunk* chunk = std::construct_at(static_cast<Chunk*>(memory));
            chunk->next = m_chunks;
            m_chunks = chunk;
            ++m_chunkCount;

            for (std::size_t index = 0; index < ComponentsPerChunk; ++index)
            {
                chunk->slots[index].nextFree = m_freeHead;
                m_freeHead = &chunk->slots[index];
            }
            return true;
        }

        bool EnsureLiveCapacity(std::size_t requiredCapacity)
        {
            if (requiredCapacity <= m_liveCapacity)
            {
                return true;
            }

            std::size_t newCapacity = m_liveCapacity == 0 ? ComponentsPerChunk : m_liveCapacity;
            while (newCapacity < requiredCapacity)
            {
                newCapacity *= 2;
            }

            void* memory = m_allocator.allocate(
                m_allocator.userData,
                sizeof(T*) * newCapacity,
                std::max(alignof(T*), alignof(void*)));
            if (memory == nullptr)
            {
                return false;
            }

            T** newLiveComponents = static_cast<T**>(memory);
            for (std::size_t index = 0; index < m_liveCount; ++index)
            {
                newLiveComponents[index] = m_liveComponents[index];
            }

            if (m_liveComponents != nullptr)
            {
                m_allocator.free(m_allocator.userData, m_liveComponents);
            }
            m_liveComponents = newLiveComponents;
            m_liveCapacity = newCapacity;
            return true;
        }

        void RemoveLiveComponent(Slot& slot)
        {
            const std::size_t removedIndex = slot.liveIndex;
            T* movedComponent = m_liveComponents[m_liveCount - 1];
            m_liveComponents[removedIndex] = movedComponent;
            --m_liveCount;

            if (removedIndex < m_liveCount)
            {
                Slot& movedSlot = *reinterpret_cast<Slot*>(movedComponent);
                movedSlot.liveIndex = removedIndex;
            }
        }

        void ReleaseMemory()
        {
            Chunk* chunk = m_chunks;
            while (chunk != nullptr)
            {
                Chunk* next = chunk->next;
                std::destroy_at(chunk);
                m_allocator.free(m_allocator.userData, chunk);
                chunk = next;
            }

            if (m_liveComponents != nullptr)
            {
                m_allocator.free(m_allocator.userData, m_liveComponents);
            }

            m_chunks = nullptr;
            m_freeHead = nullptr;
            m_liveComponents = nullptr;
            m_chunkCount = 0;
            m_liveCapacity = 0;
        }

        JAllocator m_allocator;
        Chunk* m_chunks = nullptr;
        Slot* m_freeHead = nullptr;
        T** m_liveComponents = nullptr;
        std::size_t m_chunkCount = 0;
        std::size_t m_liveCount = 0;
        std::size_t m_liveCapacity = 0;
    };
}
