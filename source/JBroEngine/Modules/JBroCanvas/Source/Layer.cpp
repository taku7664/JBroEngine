#include <JBro/Canvas/Layer.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    Layer::Layer(LayerIndex index, const char* name)
        : m_index(index)
    {
        SetName(name == nullptr ? "Layer" : name);
    }

    LayerIndex Layer::GetIndex() const
    {
        return m_index;
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
    bool Layer::IsVisible() const
    {
        return m_visible;
    }

    void Layer::SetVisible(bool visible)
    {
        m_visible = visible;
    }
}
