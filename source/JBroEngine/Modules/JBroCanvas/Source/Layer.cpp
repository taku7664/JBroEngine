#include <JBro/Canvas/Layer.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    Layer::Layer(LayerId id, const char* name)
        : m_id(id)
    {
        SetName(name == nullptr ? "Layer" : name);
    }

    LayerId Layer::GetId() const
    {
        return m_id;
    }

    const char* Layer::GetName() const
    {
        return m_name;
    }

    void Layer::SetName(const char* name)
    {
        const char* source = name == nullptr ? "" : name;
        const std::size_t length = std::min(std::strlen(source), sizeof(m_name) - 1);
        std::memcpy(m_name, source, length);
        m_name[length] = '\0';
    }

    LayerOrder Layer::GetOrder() const
    {
        return m_order;
    }

    void Layer::SetOrder(LayerOrder order)
    {
        m_order = order;
    }

    bool Layer::IsVisible() const
    {
        return m_visible;
    }

    void Layer::SetVisible(bool visible)
    {
        m_visible = visible;
    }
}
