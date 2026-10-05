#include <JBro/Canvas/Layer.h>

#include <algorithm>
#include <cmath>
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

    LayerSpace Layer::GetSpace() const
    {
        return m_space;
    }

    void Layer::SetSpace(LayerSpace space)
    {
        m_space = space;
    }

    ScreenScaleMode Layer::GetScaleMode() const
    {
        return m_scaleMode;
    }

    void Layer::SetScaleMode(ScreenScaleMode mode)
    {
        m_scaleMode = mode;
    }

    LayerBlend Layer::GetBlend() const
    {
        return m_blend;
    }

    void Layer::SetBlend(LayerBlend blend)
    {
        m_blend = blend;
    }

    float Layer::GetOpacity() const
    {
        return m_opacity;
    }

    void Layer::SetOpacity(float opacity)
    {
        if (false == std::isfinite(opacity))
        {
            return;
        }
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    bool Layer::NeedsComposite() const
    {
        return m_blend != LayerBlend::Normal || m_opacity < 1.0f;
    }

    const char* LayerBlendName(LayerBlend blend)
    {
        switch (blend)
        {
        case LayerBlend::Additive:
            return "Additive";
        case LayerBlend::Multiply:
            return "Multiply";
        case LayerBlend::Screen:
            return "Screen";
        case LayerBlend::Normal:
        default:
            return "Normal";
        }
    }

    bool ParseLayerBlend(const char* name, LayerBlend& blend)
    {
        if (name == nullptr)
        {
            return false;
        }
        const LayerBlend blends[] = {LayerBlend::Normal, LayerBlend::Additive, LayerBlend::Multiply, LayerBlend::Screen};
        for (const LayerBlend candidate : blends)
        {
            if (std::strcmp(name, LayerBlendName(candidate)) == 0)
            {
                blend = candidate;
                return true;
            }
        }
        return false;
    }
}
