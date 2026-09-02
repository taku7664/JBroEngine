#include <JBro/Core/ECS/EntityRegistry.h>

#include <algorithm>

namespace JBro::Engine
{
    namespace
    {
        constexpr std::size_t InitialEntityCapacity = 64;
        constexpr std::size_t InvalidDenseIndex = static_cast<std::size_t>(-1);
    }

    CEntityRegistry::CEntityRegistry(JAllocator allocator)
        : m_allocator(allocator)
    {
    }

    CEntityRegistry::~CEntityRegistry()
    {
        if (m_alive != nullptr) m_allocator.free(m_allocator.userData, m_alive);
        if (m_denseIndices != nullptr) m_allocator.free(m_allocator.userData, m_denseIndices);
        if (m_liveEntities != nullptr) m_allocator.free(m_allocator.userData, m_liveEntities);
    }

    Entity CEntityRegistry::Create()
    {
        if (m_nextEntity == InvalidEntity || false == EnsureEntityCapacity(static_cast<std::size_t>(m_nextEntity) + 1) ||
            false == EnsureLiveCapacity(m_liveCount + 1))
        {
            return InvalidEntity;
        }

        const Entity entity = m_nextEntity++;
        m_alive[entity] = true;
        m_denseIndices[entity] = m_liveCount;
        m_liveEntities[m_liveCount++] = entity;
        return entity;
    }

    bool CEntityRegistry::Destroy(Entity entity)
    {
        if (false == IsAlive(entity))
        {
            return false;
        }

        const std::size_t removedIndex = m_denseIndices[entity];
        const Entity movedEntity = m_liveEntities[m_liveCount - 1];
        m_liveEntities[removedIndex] = movedEntity;
        --m_liveCount;
        if (removedIndex < m_liveCount)
        {
            m_denseIndices[movedEntity] = removedIndex;
        }
        m_alive[entity] = false;
        m_denseIndices[entity] = InvalidDenseIndex;
        return true;
    }

    bool CEntityRegistry::IsAlive(Entity entity) const
    {
        return entity != InvalidEntity && entity < m_nextEntity && m_alive != nullptr && m_alive[entity];
    }

    void CEntityRegistry::Clear()
    {
        for (std::size_t index = 0; index < m_liveCount; ++index)
        {
            const Entity entity = m_liveEntities[index];
            m_alive[entity] = false;
            m_denseIndices[entity] = InvalidDenseIndex;
        }
        m_liveCount = 0;
    }

    std::size_t CEntityRegistry::GetLiveCount() const
    {
        return m_liveCount;
    }

    bool CEntityRegistry::EnsureEntityCapacity(std::size_t capacity)
    {
        if (capacity <= m_entityCapacity) return true;
        if (m_allocator.allocate == nullptr || m_allocator.free == nullptr) return false;
        std::size_t newCapacity = m_entityCapacity == 0 ? InitialEntityCapacity : m_entityCapacity;
        while (newCapacity < capacity) newCapacity *= 2;

        bool* newAlive = static_cast<bool*>(m_allocator.allocate(m_allocator.userData, sizeof(bool) * newCapacity, alignof(bool)));
        std::size_t* newDense = static_cast<std::size_t*>(m_allocator.allocate(
            m_allocator.userData, sizeof(std::size_t) * newCapacity, alignof(std::size_t)));
        if (newAlive == nullptr || newDense == nullptr)
        {
            if (newAlive != nullptr) m_allocator.free(m_allocator.userData, newAlive);
            if (newDense != nullptr) m_allocator.free(m_allocator.userData, newDense);
            return false;
        }

        std::fill_n(newAlive, newCapacity, false);
        std::fill_n(newDense, newCapacity, InvalidDenseIndex);
        if (m_entityCapacity > 0)
        {
            std::copy_n(m_alive, m_entityCapacity, newAlive);
            std::copy_n(m_denseIndices, m_entityCapacity, newDense);
        }
        if (m_alive != nullptr) m_allocator.free(m_allocator.userData, m_alive);
        if (m_denseIndices != nullptr) m_allocator.free(m_allocator.userData, m_denseIndices);
        m_alive = newAlive;
        m_denseIndices = newDense;
        m_entityCapacity = newCapacity;
        return true;
    }

    bool CEntityRegistry::EnsureLiveCapacity(std::size_t capacity)
    {
        if (capacity <= m_liveCapacity) return true;
        if (m_allocator.allocate == nullptr || m_allocator.free == nullptr) return false;
        std::size_t newCapacity = m_liveCapacity == 0 ? InitialEntityCapacity : m_liveCapacity;
        while (newCapacity < capacity) newCapacity *= 2;
        Entity* newLive = static_cast<Entity*>(m_allocator.allocate(
            m_allocator.userData, sizeof(Entity) * newCapacity, alignof(Entity)));
        if (newLive == nullptr) return false;
        if (m_liveCount > 0)
        {
            std::copy_n(m_liveEntities, m_liveCount, newLive);
        }
        if (m_liveEntities != nullptr) m_allocator.free(m_allocator.userData, m_liveEntities);
        m_liveEntities = newLive;
        m_liveCapacity = newCapacity;
        return true;
    }
}
