#pragma once

#include <JBro/Core/Core.h>

#include <cstddef>

namespace JBro::Engine
{
    class CEntityRegistry
    {
    public:
        explicit CEntityRegistry(JAllocator allocator);
        ~CEntityRegistry();

        CEntityRegistry(const CEntityRegistry&) = delete;
        CEntityRegistry& operator=(const CEntityRegistry&) = delete;

        Entity Create();
        bool Destroy(Entity entity);
        bool IsAlive(Entity entity) const;
        void Clear();

        std::size_t GetLiveCount() const;

        template <typename Fn>
        void ForEachAlive(Fn&& function) const
        {
            for (std::size_t index = 0; index < m_liveCount; ++index)
            {
                function(m_liveEntities[index]);
            }
        }

    private:
        bool EnsureEntityCapacity(std::size_t capacity);
        bool EnsureLiveCapacity(std::size_t capacity);

        JAllocator m_allocator;
        bool* m_alive = nullptr;
        std::size_t* m_denseIndices = nullptr;
        Entity* m_liveEntities = nullptr;
        std::size_t m_entityCapacity = 0;
        std::size_t m_liveCapacity = 0;
        std::size_t m_liveCount = 0;
        Entity m_nextEntity = 0;
    };
}
