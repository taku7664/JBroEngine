#include <JBro/Framework2D/Canvas/Layer.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    Layer::Layer(LayerIndex index, const char* name)
        : m_index(index)
    {
        SetName(name == nullptr ? "Layer" : name);
    }

    LayerIndex     Layer::GetIndex()      const { return m_index; }
    const char*    Layer::GetName()       const { return m_name; }
    void           Layer::SetName(const char* name)
    {
        const char* source = name == nullptr ? "" : name;
        const std::size_t length = std::min(std::strlen(source), sizeof(m_name) - 1);
        std::memcpy(m_name, source, length);
        m_name[length] = '\0';
    }
    LayerBlendMode Layer::GetBlendMode()  const               { return m_blendMode; }
    void           Layer::SetBlendMode(LayerBlendMode mode)   { m_blendMode = mode; }
    LayerSpace     Layer::GetSpace()      const               { return m_space; }
    void           Layer::SetSpace(LayerSpace space)          { m_space = space; }
    float          Layer::GetOpacity()    const               { return m_opacity; }
    void           Layer::SetOpacity(float opacity)           { m_opacity = std::clamp(opacity, 0.0f, 1.0f); }
    bool           Layer::IsVisible()     const               { return m_visible; }
    void           Layer::SetVisible(bool visible)            { m_visible = visible; }
    float          Layer::GetParallaxFactor() const           { return m_parallaxFactor; }
    void           Layer::SetParallaxFactor(float factor)     { m_parallaxFactor = std::max(factor, 0.0f); }
    bool           Layer::ForcesOwnTexture() const            { return m_forceOwnTexture; }
    void           Layer::SetForceOwnTexture(bool enabled)    { m_forceOwnTexture = enabled; }
}
