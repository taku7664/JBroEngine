#include <JBro/Canvas/Layer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

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

    Bool Layer::IsVisible() const
    {
        return m_visible;
    }

    void Layer::SetVisible(Bool visible)
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

    Float Layer::GetOpacity() const
    {
        return m_opacity;
    }

    void Layer::SetOpacity(Float opacity)
    {
        if (false == std::isfinite(opacity))
        {
            return;
        }
        m_opacity = JBro::Clamp(opacity, 0.0f, 1.0f);
    }

    Bool Layer::NeedsComposite() const
    {
        return m_blend != LayerBlend::Normal || m_opacity < 1.0f;
    }

    const Uuid& Layer::GetSourceAsset() const
    {
        return m_sourceAsset;
    }

    void Layer::SetSourceAsset(const Uuid& asset)
    {
        m_sourceAsset = asset;
    }

    Float Layer::GetParallax() const
    {
        return m_parallax;
    }

    void Layer::SetParallax(Float factor)
    {
        if (false == std::isfinite(factor) || factor < 0.0f)
        {
            return;
        }
        m_parallax = factor;
    }

    Bool Layer::IsLit() const
    {
        return m_lit;
    }

    void Layer::SetLit(Bool lit)
    {
        m_lit = lit;
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
        case LayerBlend::Subtract:
            return "Subtract";
        case LayerBlend::Lighten:
            return "Lighten";
        case LayerBlend::Darken:
            return "Darken";
        case LayerBlend::Overlay:
            return "Overlay";
        case LayerBlend::SoftLight:
            return "SoftLight";
        case LayerBlend::HardLight:
            return "HardLight";
        case LayerBlend::ColorDodge:
            return "ColorDodge";
        case LayerBlend::ColorBurn:
            return "ColorBurn";
        case LayerBlend::Difference:
            return "Difference";
        case LayerBlend::Normal:
        default:
            return "Normal";
        }
    }

    Bool ParseLayerBlend(const char* name, LayerBlend& blend)
    {
        if (name == nullptr)
        {
            return false;
        }
        for (UInt32 at = 0; at < LayerBlendCount; ++at)
        {
            const LayerBlend candidate = static_cast<LayerBlend>(at.Get());
            if (std::strcmp(name, LayerBlendName(candidate)) == 0)
            {
                blend = candidate;
                return true;
            }
        }
        return false;
    }
}
