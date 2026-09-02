#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Canvas/Layer.h>

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace JBro::Engine
{
    class CWorld;

    class CCanvas final
    {
    public:
        explicit CCanvas(JAllocator allocator);

        CWorld& GetWorld();
        const CWorld& GetWorld() const;

        CLayer& CreateLayer(const char* name = nullptr);
        bool DestroyLayer(LayerId layer);
        bool MoveLayer(LayerId layer, std::size_t newIndex);
        CLayer* FindLayer(LayerId layer);
        const CLayer* FindLayer(LayerId layer) const;
        CLayer* FindLayerByName(const char* name);
        std::size_t GetLayerCount() const;
        CLayer* GetLayerAt(std::size_t index);
        const CLayer* GetLayerAt(std::size_t index) const;
        LayerId GetDefaultLayerId() const;

        bool AssignEntity(Entity entity, LayerId layer);
        LayerId GetEntityLayer(Entity entity) const;
        void RemoveEntity(Entity entity);
        void PruneDeadEntities();

    private:
        OwnerPtr<CWorld> m_world;
        std::vector<OwnerPtr<CLayer>> m_layers;
        std::unordered_map<Entity, LayerId> m_entityLayers;
        LayerId m_defaultLayer = InvalidLayerId;
        LayerId m_nextLayer = 0;
    };
}
