#pragma once

#include <JBro/Core/ECS/ComponentPool.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace JBro::Engine
{
    class IComponentStorage
    {
    public:
        virtual ~IComponentStorage() = default;

        virtual ComponentTypeId GetTypeId() const = 0;
        virtual bool Has(Entity entity) const = 0;
        virtual bool Remove(Entity entity) = 0;
        virtual void Clear() = 0;
        virtual std::size_t GetCount() const = 0;
    };

    template <typename T>
    class TComponentStorage final : public IComponentStorage
    {
    public:
        TComponentStorage(ComponentTypeId typeId, JAllocator allocator)
            : m_typeId(typeId)
            , m_allocator(allocator)
            , m_pool(allocator)
        {
        }

        ~TComponentStorage() override
        {
            Clear();
            if (m_sparse != nullptr) m_allocator.free(m_allocator.userData, m_sparse);
            if (m_denseEntities != nullptr) m_allocator.free(m_allocator.userData, m_denseEntities);
        }

        template <typename... Args>
        T* Add(Entity entity, Args&&... args)
        {
            if (entity == InvalidEntity || m_iterationDepth > 0)
            {
                return nullptr;
            }
            if (Has(entity))
            {
                return m_sparse[entity].component;
            }
            if (false == EnsureSparseCapacity(static_cast<std::size_t>(entity) + 1) ||
                false == EnsureDenseCapacity(m_count + 1))
            {
                return nullptr;
            }

            T* component = m_pool.Create(std::forward<Args>(args)...);
            if (component == nullptr)
            {
                return nullptr;
            }
            m_sparse[entity] = SparseEntry{ component, m_count };
            m_denseEntities[m_count++] = entity;
            return component;
        }

        T* Get(Entity entity)
        {
            return Has(entity) ? m_sparse[entity].component : nullptr;
        }

        const T* Get(Entity entity) const
        {
            return Has(entity) ? m_sparse[entity].component : nullptr;
        }

        ComponentTypeId GetTypeId() const override
        {
            return m_typeId;
        }

        bool Has(Entity entity) const override
        {
            return entity != InvalidEntity && entity < m_sparseCapacity && m_sparse[entity].component != nullptr;
        }

        bool Remove(Entity entity) override
        {
            if (m_iterationDepth > 0 || false == Has(entity))
            {
                return false;
            }

            SparseEntry& removed = m_sparse[entity];
            const std::size_t removedIndex = removed.denseIndex;
            const Entity movedEntity = m_denseEntities[m_count - 1];
            T* component = removed.component;
            m_denseEntities[removedIndex] = movedEntity;
            --m_count;
            if (removedIndex < m_count)
            {
                m_sparse[movedEntity].denseIndex = removedIndex;
            }
            removed = {};
            return m_pool.Destroy(component);
        }

        void Clear() override
        {
            while (m_count > 0)
            {
                Remove(m_denseEntities[m_count - 1]);
            }
        }

        std::size_t GetCount() const override
        {
            return m_count;
        }

        template <typename Fn>
        void ForEach(Fn&& function)
        {
            ++m_iterationDepth;
            try
            {
                for (std::size_t index = 0; index < m_count; ++index)
                {
                    const Entity entity = m_denseEntities[index];
                    function(entity, *m_sparse[entity].component);
                }
            }
            catch (...)
            {
                --m_iterationDepth;
                throw;
            }
            --m_iterationDepth;
        }

    private:
        struct SparseEntry
        {
            T* component = nullptr;
            std::size_t denseIndex = 0;
        };

        bool EnsureSparseCapacity(std::size_t capacity)
        {
            if (capacity <= m_sparseCapacity) return true;
            std::size_t newCapacity = m_sparseCapacity == 0 ? 64 : m_sparseCapacity;
            while (newCapacity < capacity) newCapacity *= 2;
            SparseEntry* entries = static_cast<SparseEntry*>(m_allocator.allocate(
                m_allocator.userData, sizeof(SparseEntry) * newCapacity, alignof(SparseEntry)));
            if (entries == nullptr) return false;
            std::fill_n(entries, newCapacity, SparseEntry{});
            if (m_sparseCapacity > 0)
            {
                std::copy_n(m_sparse, m_sparseCapacity, entries);
            }
            if (m_sparse != nullptr) m_allocator.free(m_allocator.userData, m_sparse);
            m_sparse = entries;
            m_sparseCapacity = newCapacity;
            return true;
        }

        bool EnsureDenseCapacity(std::size_t capacity)
        {
            if (capacity <= m_denseCapacity) return true;
            std::size_t newCapacity = m_denseCapacity == 0 ? 64 : m_denseCapacity;
            while (newCapacity < capacity) newCapacity *= 2;
            Entity* entities = static_cast<Entity*>(m_allocator.allocate(
                m_allocator.userData, sizeof(Entity) * newCapacity, alignof(Entity)));
            if (entities == nullptr) return false;
            if (m_count > 0)
            {
                std::copy_n(m_denseEntities, m_count, entities);
            }
            if (m_denseEntities != nullptr) m_allocator.free(m_allocator.userData, m_denseEntities);
            m_denseEntities = entities;
            m_denseCapacity = newCapacity;
            return true;
        }

        ComponentTypeId m_typeId;
        JAllocator m_allocator;
        TComponentPool<T> m_pool;
        SparseEntry* m_sparse = nullptr;
        Entity* m_denseEntities = nullptr;
        std::size_t m_sparseCapacity = 0;
        std::size_t m_denseCapacity = 0;
        std::size_t m_count = 0;
        std::size_t m_iterationDepth = 0;
    };
}
