#include <JBro/Framework2D/Canvas/Layer.h>

#include <algorithm>
#include <cstring>

namespace JBro::Engine
{
    CLayer::CLayer(LayerId id, const char* name)
        : m_id(id)
    {
        SetName(name == nullptr ? "Layer" : name);
    }

    LayerId CLayer::GetId() const { return m_id; }
    const char* CLayer::GetName() const { return m_name; }
    void CLayer::SetName(const char* name)
    {
        const char* source = name == nullptr ? "" : name;
        const std::size_t length = std::min(std::strlen(source), sizeof(m_name) - 1);
        std::memcpy(m_name, source, length);
        m_name[length] = '\0';
    }
    ELayerBlendMode CLayer::GetBlendMode() const { return m_blendMode; }
    void CLayer::SetBlendMode(ELayerBlendMode mode) { m_blendMode = mode; }
    ELayerSpace CLayer::GetSpace() const { return m_space; }
    void CLayer::SetSpace(ELayerSpace space) { m_space = space; }
    float CLayer::GetOpacity() const { return m_opacity; }
    void CLayer::SetOpacity(float opacity) { m_opacity = std::clamp(opacity, 0.0f, 1.0f); }
    bool CLayer::IsVisible() const { return m_visible; }
    void CLayer::SetVisible(bool visible) { m_visible = visible; }
    float CLayer::GetParallaxFactor() const { return m_parallaxFactor; }
    void CLayer::SetParallaxFactor(float factor) { m_parallaxFactor = std::max(factor, 0.0f); }
    bool CLayer::ForcesOwnTexture() const { return m_forceOwnTexture; }
    void CLayer::SetForceOwnTexture(bool enabled) { m_forceOwnTexture = enabled; }
}
