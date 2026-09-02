#include <JBro/Framework2D/Canvas/Canvas.h>

#include <JBro/Runtime/World.h>

#include <algorithm>
#include <cstring>

namespace JBro::Engine
{
    CCanvas::CCanvas(JAllocator allocator)
        : m_world(MakeOwnerPtr<CWorld>(allocator))
    {
    }

    CWorld& CCanvas::GetWorld() { return *m_world; }
    const CWorld& CCanvas::GetWorld() const { return *m_world; }

    CLayer& CCanvas::CreateLayer(const char* name)
    {
        OwnerPtr<CLayer> layer = MakeOwnerPtr<CLayer>(m_nextLayer++, name == nullptr ? "Layer" : name);
        CLayer& result = *layer;
        m_layers.push_back(std::move(layer));
        if (m_defaultLayer == InvalidLayerId) m_defaultLayer = result.GetId();
        return result;
    }

    bool CCanvas::DestroyLayer(LayerId layer)
    {
        const auto found = std::find_if(m_layers.begin(), m_layers.end(), [layer](const OwnerPtr<CLayer>& item)
        {
            return item->GetId() == layer;
        });
        if (found == m_layers.end() || m_layers.size() == 1) return false;
        const bool wasDefault = layer == m_defaultLayer;
        m_layers.erase(found);
        if (wasDefault) m_defaultLayer = m_layers.front()->GetId();
        for (auto& [entity, assignedLayer] : m_entityLayers)
        {
            if (assignedLayer == layer) assignedLayer = m_defaultLayer;
        }
        return true;
    }

    bool CCanvas::MoveLayer(LayerId layer, std::size_t newIndex)
    {
        const auto found = std::find_if(m_layers.begin(), m_layers.end(), [layer](const OwnerPtr<CLayer>& item)
        {
            return item->GetId() == layer;
        });
        if (found == m_layers.end() || newIndex >= m_layers.size()) return false;
        OwnerPtr<CLayer> moved = std::move(*found);
        m_layers.erase(found);
        m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
        return true;
    }

    CLayer* CCanvas::FindLayer(LayerId layer)
    {
        const auto found = std::find_if(m_layers.begin(), m_layers.end(), [layer](const OwnerPtr<CLayer>& item)
        {
            return item->GetId() == layer;
        });
        return found == m_layers.end() ? nullptr : found->get();
    }

    const CLayer* CCanvas::FindLayer(LayerId layer) const
    {
        const auto found = std::find_if(m_layers.begin(), m_layers.end(), [layer](const OwnerPtr<CLayer>& item)
        {
            return item->GetId() == layer;
        });
        return found == m_layers.end() ? nullptr : found->get();
    }

    CLayer* CCanvas::FindLayerByName(const char* name)
    {
        if (name == nullptr) return nullptr;
        const auto found = std::find_if(m_layers.begin(), m_layers.end(), [name](const OwnerPtr<CLayer>& item)
        {
            return std::strcmp(item->GetName(), name) == 0;
        });
        return found == m_layers.end() ? nullptr : found->get();
    }

    std::size_t CCanvas::GetLayerCount() const { return m_layers.size(); }
    CLayer* CCanvas::GetLayerAt(std::size_t index) { return index < m_layers.size() ? m_layers[index].get() : nullptr; }
    const CLayer* CCanvas::GetLayerAt(std::size_t index) const { return index < m_layers.size() ? m_layers[index].get() : nullptr; }
    LayerId CCanvas::GetDefaultLayerId() const { return m_defaultLayer; }

    bool CCanvas::AssignEntity(Entity entity, LayerId layer)
    {
        if (false == m_world->IsAlive(entity) || FindLayer(layer) == nullptr) return false;
        m_entityLayers[entity] = layer;
        return true;
    }

    LayerId CCanvas::GetEntityLayer(Entity entity) const
    {
        if (false == m_world->IsAlive(entity)) return InvalidLayerId;
        const auto found = m_entityLayers.find(entity);
        return found == m_entityLayers.end() ? InvalidLayerId : found->second;
    }

    void CCanvas::RemoveEntity(Entity entity)
    {
        m_entityLayers.erase(entity);
    }

    void CCanvas::PruneDeadEntities()
    {
        for (auto iterator = m_entityLayers.begin(); iterator != m_entityLayers.end();)
        {
            if (false == m_world->IsAlive(iterator->first)) iterator = m_entityLayers.erase(iterator);
            else ++iterator;
        }
    }
}
