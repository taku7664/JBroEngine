#include <JBro/Framework2D/Canvas/Canvas.h>

#include <JBro/Runtime/Component.h>

#include <algorithm>

namespace JBro
{
    Canvas::Canvas(JAllocator allocator)
        : m_allocator(allocator)
        , m_objects(std::make_unique<TObjectPool<GameObject>>(allocator))
    {
        CreateLayer("Default");
    }

    Canvas::~Canvas() = default;

    GameObject* Canvas::CreateObject(const char*)
    {
        // F1~G4 에서 실제 풀 구현이 완성되면 여기서 Create() 하고 SetCanvas(this) 한다.
        // B0/B 골격 단계에서는 시그니처만 확정한다.
        return nullptr;
    }

    bool Canvas::DestroyObject(GameObject*) { return false; }
    std::size_t Canvas::GetObjectCount() const { return 0; }

    Layer& Canvas::CreateLayer(const char* name)
    {
        auto layer = std::make_unique<Layer>(m_nextLayer++, name == nullptr ? "Layer" : name);
        Layer& ref = *layer;
        m_layers.push_back(std::move(layer));
        if (m_defaultLayer == InvalidLayerIndex) m_defaultLayer = ref.GetIndex();
        return ref;
    }

    bool Canvas::DestroyLayer(LayerIndex layer)
    {
        if (m_layers.size() <= 1) return false;
        const auto found = std::find_if(m_layers.begin(), m_layers.end(),
            [layer](const std::unique_ptr<Layer>& item) { return item->GetIndex() == layer; });
        if (found == m_layers.end()) return false;
        const bool wasDefault = layer == m_defaultLayer;
        m_layers.erase(found);
        if (wasDefault) m_defaultLayer = m_layers.front()->GetIndex();
        return true;
    }

    bool Canvas::MoveLayer(LayerIndex layer, std::size_t newIndex)
    {
        const auto found = std::find_if(m_layers.begin(), m_layers.end(),
            [layer](const std::unique_ptr<Layer>& item) { return item->GetIndex() == layer; });
        if (found == m_layers.end() || newIndex >= m_layers.size()) return false;
        auto owner = std::move(*found);
        m_layers.erase(found);
        m_layers.insert(m_layers.begin() + newIndex, std::move(owner));
        return true;
    }

    Layer*      Canvas::FindLayer(LayerIndex layer)
    {
        const auto found = std::find_if(m_layers.begin(), m_layers.end(),
            [layer](const std::unique_ptr<Layer>& item) { return item->GetIndex() == layer; });
        return found == m_layers.end() ? nullptr : found->get();
    }

    std::size_t Canvas::GetLayerCount() const { return m_layers.size(); }
    Layer*      Canvas::GetLayerAt(std::size_t index) { return index < m_layers.size() ? m_layers[index].get() : nullptr; }
    LayerIndex  Canvas::GetDefaultLayer() const { return m_defaultLayer; }
}
