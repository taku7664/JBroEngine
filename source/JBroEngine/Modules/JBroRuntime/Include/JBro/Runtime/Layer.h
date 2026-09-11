#pragma once

#include <cstdint>

namespace JBro
{
    using LayerIndex = std::uint32_t;
    inline constexpr LayerIndex InvalidLayerIndex = static_cast<LayerIndex>(-1);

    class Layer final
    {
    public:
        Layer(LayerIndex index, const char* name);

        LayerIndex     GetIndex() const;
        const char*    GetName()  const;
        void           SetName(const char* name);

        bool           IsVisible()    const;
        void           SetVisible(bool visible);

    private:
        LayerIndex m_index = InvalidLayerIndex;
        char       m_name[64]{};
        bool       m_visible = true;
    };
}
