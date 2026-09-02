#include <JBro/Framework2D/Canvas/Canvas.h>

#include <JBro/Runtime/Component.h>

#include <utility>

namespace JBro
{
    Canvas::Canvas(JAllocator allocator)
        : m_allocator(allocator)
        , m_objects(MakeOwnerPtr<TObjectPool<GameObject>>(allocator))
    {
        CreateLayer("Default");
    }

    Canvas::~Canvas() = default;

    GameObject* Canvas::CreateObject(const char*)
    {
        // 실제 풀 구현이 완성되면 여기서 Create() 하고 SetCanvas(this) 한다.
        return nullptr;
    }

    bool        Canvas::DestroyObject(GameObject*) { return false; }
    std::size_t Canvas::GetObjectCount() const     { return 0; }

    Layer& Canvas::CreateLayer(const char* name)
    {
        auto layer = MakeOwnerPtr<Layer>(m_nextLayer++, name == nullptr ? "Layer" : name);
        Layer& ref = *layer;
        m_layers.Add(std::move(layer));
        if (m_defaultLayer == InvalidLayerIndex) m_defaultLayer = ref.GetIndex();
        return ref;
    }

    bool Canvas::DestroyLayer(LayerIndex layer)
    {
        if (m_layers.Size() <= 1) return false;
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item) { return item->GetIndex() == layer; });
        if (index == m_layers.Size()) return false;
        const bool wasDefault = layer == m_defaultLayer;
        m_layers.RemoveAt(index);
        if (wasDefault) m_defaultLayer = m_layers.First()->GetIndex();
        return true;
    }

    bool Canvas::MoveLayer(LayerIndex layer, std::size_t newIndex)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item) { return item->GetIndex() == layer; });
        if (index == m_layers.Size() || newIndex >= m_layers.Size()) return false;
        OwnerPtr<Layer> owner = std::move(m_layers[index]);
        m_layers.RemoveAt(index);
        m_layers.Insert(newIndex, std::move(owner));
        return true;
    }

    Layer* Canvas::FindLayer(LayerIndex layer)
    {
        const std::size_t index = m_layers.IndexOfBy(
            [layer](const OwnerPtr<Layer>& item) { return item->GetIndex() == layer; });
        return index == m_layers.Size() ? nullptr : m_layers[index].get();
    }

    std::size_t Canvas::GetLayerCount() const                          { return m_layers.Size(); }
    Layer*      Canvas::GetLayerAt(std::size_t index)                  { return index < m_layers.Size() ? m_layers[index].get() : nullptr; }
    LayerIndex  Canvas::GetDefaultLayer() const                        { return m_defaultLayer; }
}
