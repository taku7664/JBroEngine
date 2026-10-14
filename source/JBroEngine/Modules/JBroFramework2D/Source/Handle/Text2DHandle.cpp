#include <JBro/Framework2D/Handle/Text2D.h>

#include <JBro/Framework2D/Component/Text2D.h>

namespace JBro::Handle
{
    Component::Text2D* Text2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Text2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    AssetId Text2D::GetFontId() const
    {
        const Component::Text2D* component = Resolve("GetFontId");
        if (component == nullptr)
        {
            return {};
        }
        return component->fontId;
    }

    AssetHandle Text2D::GetFont() const
    {
        const Component::Text2D* component = Resolve("GetFont");
        if (component == nullptr)
        {
            return {};
        }
        return component->font;
    }

    void Text2D::SetFont(AssetHandle value)
    {
        Component::Text2D* component = Resolve("SetFont");
        if (component == nullptr)
        {
            return;
        }
        component->font = value;
    }

    Float Text2D::GetFontSize() const
    {
        const Component::Text2D* component = Resolve("GetFontSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->fontSize;
    }

    void Text2D::SetFontSize(Float value)
    {
        Component::Text2D* component = Resolve("SetFontSize");
        if (component == nullptr)
        {
            return;
        }
        component->fontSize = value;
    }

    Vector2 Text2D::GetBoxSize() const
    {
        const Component::Text2D* component = Resolve("GetBoxSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->boxSize;
    }

    void Text2D::SetBoxSize(Vector2 value)
    {
        Component::Text2D* component = Resolve("SetBoxSize");
        if (component == nullptr)
        {
            return;
        }
        component->boxSize = value;
    }

    Component::TextOverflow Text2D::GetOverflow() const
    {
        const Component::Text2D* component = Resolve("GetOverflow");
        if (component == nullptr)
        {
            return {};
        }
        return component->overflow;
    }

    void Text2D::SetOverflow(Component::TextOverflow value)
    {
        Component::Text2D* component = Resolve("SetOverflow");
        if (component == nullptr)
        {
            return;
        }
        component->overflow = value;
    }

    Component::TextWrapMode Text2D::GetWrapMode() const
    {
        const Component::Text2D* component = Resolve("GetWrapMode");
        if (component == nullptr)
        {
            return {};
        }
        return component->wrapMode;
    }

    void Text2D::SetWrapMode(Component::TextWrapMode value)
    {
        Component::Text2D* component = Resolve("SetWrapMode");
        if (component == nullptr)
        {
            return;
        }
        component->wrapMode = value;
    }

    Component::TextAlignX Text2D::GetAlignX() const
    {
        const Component::Text2D* component = Resolve("GetAlignX");
        if (component == nullptr)
        {
            return {};
        }
        return component->alignX;
    }

    void Text2D::SetAlignX(Component::TextAlignX value)
    {
        Component::Text2D* component = Resolve("SetAlignX");
        if (component == nullptr)
        {
            return;
        }
        component->alignX = value;
    }

    Component::TextAlignY Text2D::GetAlignY() const
    {
        const Component::Text2D* component = Resolve("GetAlignY");
        if (component == nullptr)
        {
            return {};
        }
        return component->alignY;
    }

    void Text2D::SetAlignY(Component::TextAlignY value)
    {
        Component::Text2D* component = Resolve("SetAlignY");
        if (component == nullptr)
        {
            return;
        }
        component->alignY = value;
    }

    Float Text2D::GetLineSpacing() const
    {
        const Component::Text2D* component = Resolve("GetLineSpacing");
        if (component == nullptr)
        {
            return {};
        }
        return component->lineSpacing;
    }

    void Text2D::SetLineSpacing(Float value)
    {
        Component::Text2D* component = Resolve("SetLineSpacing");
        if (component == nullptr)
        {
            return;
        }
        component->lineSpacing = value;
    }

    Float Text2D::GetLetterSpacing() const
    {
        const Component::Text2D* component = Resolve("GetLetterSpacing");
        if (component == nullptr)
        {
            return {};
        }
        return component->letterSpacing;
    }

    void Text2D::SetLetterSpacing(Float value)
    {
        Component::Text2D* component = Resolve("SetLetterSpacing");
        if (component == nullptr)
        {
            return;
        }
        component->letterSpacing = value;
    }

    Color Text2D::GetColor() const
    {
        const Component::Text2D* component = Resolve("GetColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->color;
    }

    void Text2D::SetColor(Color value)
    {
        Component::Text2D* component = Resolve("SetColor");
        if (component == nullptr)
        {
            return;
        }
        component->color = value;
    }

    Int32 Text2D::GetRenderOrder() const
    {
        const Component::Text2D* component = Resolve("GetRenderOrder");
        if (component == nullptr)
        {
            return {};
        }
        return component->renderOrder;
    }

    void Text2D::SetRenderOrder(Int32 value)
    {
        Component::Text2D* component = Resolve("SetRenderOrder");
        if (component == nullptr)
        {
            return;
        }
        component->renderOrder = value;
    }

    Bool Text2D::GetVisible() const
    {
        const Component::Text2D* component = Resolve("GetVisible");
        if (component == nullptr)
        {
            return {};
        }
        return component->visible;
    }

    void Text2D::SetVisible(Bool value)
    {
        Component::Text2D* component = Resolve("SetVisible");
        if (component == nullptr)
        {
            return;
        }
        component->visible = value;
    }

    Color Text2D::GetOutlineColor() const
    {
        const Component::Text2D* component = Resolve("GetOutlineColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->outlineColor;
    }

    void Text2D::SetOutlineColor(Color value)
    {
        Component::Text2D* component = Resolve("SetOutlineColor");
        if (component == nullptr)
        {
            return;
        }
        component->outlineColor = value;
    }

    Float Text2D::GetOutlineWidth() const
    {
        const Component::Text2D* component = Resolve("GetOutlineWidth");
        if (component == nullptr)
        {
            return {};
        }
        return component->outlineWidth;
    }

    void Text2D::SetOutlineWidth(Float value)
    {
        Component::Text2D* component = Resolve("SetOutlineWidth");
        if (component == nullptr)
        {
            return;
        }
        component->outlineWidth = value;
    }

    Bool Text2D::GetAutoSize() const
    {
        const Component::Text2D* component = Resolve("GetAutoSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->autoSize;
    }

    void Text2D::SetAutoSize(Bool value)
    {
        Component::Text2D* component = Resolve("SetAutoSize");
        if (component == nullptr)
        {
            return;
        }
        component->autoSize = value;
    }

    Float Text2D::GetMinFontSize() const
    {
        const Component::Text2D* component = Resolve("GetMinFontSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->minFontSize;
    }

    void Text2D::SetMinFontSize(Float value)
    {
        Component::Text2D* component = Resolve("SetMinFontSize");
        if (component == nullptr)
        {
            return;
        }
        component->minFontSize = value;
    }

    Float Text2D::GetMaxFontSize() const
    {
        const Component::Text2D* component = Resolve("GetMaxFontSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->maxFontSize;
    }

    void Text2D::SetMaxFontSize(Float value)
    {
        Component::Text2D* component = Resolve("SetMaxFontSize");
        if (component == nullptr)
        {
            return;
        }
        component->maxFontSize = value;
    }

    Bool Text2D::GetPixelSnap() const
    {
        const Component::Text2D* component = Resolve("GetPixelSnap");
        if (component == nullptr)
        {
            return {};
        }
        return component->pixelSnap;
    }

    void Text2D::SetPixelSnap(Bool value)
    {
        Component::Text2D* component = Resolve("SetPixelSnap");
        if (component == nullptr)
        {
            return;
        }
        component->pixelSnap = value;
    }

    Bool Text2D::GetRichText() const
    {
        const Component::Text2D* component = Resolve("GetRichText");
        if (component == nullptr)
        {
            return {};
        }
        return component->richText;
    }

    void Text2D::SetRichText(Bool value)
    {
        Component::Text2D* component = Resolve("SetRichText");
        if (component == nullptr)
        {
            return;
        }
        component->richText = value;
    }
}
