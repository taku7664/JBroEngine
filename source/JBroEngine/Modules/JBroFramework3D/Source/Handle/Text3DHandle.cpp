#include <JBro/Framework3D/Handle/Text3D.h>

#include <JBro/Framework3D/Component/Text3D.h>

namespace JBro::Handle
{
    Component::Text3D* Text3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Text3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    AssetId Text3D::GetFontId() const
    {
        const Component::Text3D* component = Resolve("GetFontId");
        if (component == nullptr)
        {
            return {};
        }
        return component->fontId;
    }

    AssetHandle Text3D::GetFont() const
    {
        const Component::Text3D* component = Resolve("GetFont");
        if (component == nullptr)
        {
            return {};
        }
        return component->font;
    }

    void Text3D::SetFont(AssetHandle value)
    {
        Component::Text3D* component = Resolve("SetFont");
        if (component == nullptr)
        {
            return;
        }
        component->font = value;
    }

    Float Text3D::GetFontSize() const
    {
        const Component::Text3D* component = Resolve("GetFontSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->fontSize;
    }

    void Text3D::SetFontSize(Float value)
    {
        Component::Text3D* component = Resolve("SetFontSize");
        if (component == nullptr)
        {
            return;
        }
        component->fontSize = value;
    }

    Float Text3D::GetBoxWidth() const
    {
        const Component::Text3D* component = Resolve("GetBoxWidth");
        if (component == nullptr)
        {
            return {};
        }
        return component->boxWidth;
    }

    void Text3D::SetBoxWidth(Float value)
    {
        Component::Text3D* component = Resolve("SetBoxWidth");
        if (component == nullptr)
        {
            return;
        }
        component->boxWidth = value;
    }

    Float Text3D::GetBoxHeight() const
    {
        const Component::Text3D* component = Resolve("GetBoxHeight");
        if (component == nullptr)
        {
            return {};
        }
        return component->boxHeight;
    }

    void Text3D::SetBoxHeight(Float value)
    {
        Component::Text3D* component = Resolve("SetBoxHeight");
        if (component == nullptr)
        {
            return;
        }
        component->boxHeight = value;
    }

    Component::TextOverflow Text3D::GetOverflow() const
    {
        const Component::Text3D* component = Resolve("GetOverflow");
        if (component == nullptr)
        {
            return {};
        }
        return component->overflow;
    }

    void Text3D::SetOverflow(Component::TextOverflow value)
    {
        Component::Text3D* component = Resolve("SetOverflow");
        if (component == nullptr)
        {
            return;
        }
        component->overflow = value;
    }

    Component::TextWrapMode Text3D::GetWrapMode() const
    {
        const Component::Text3D* component = Resolve("GetWrapMode");
        if (component == nullptr)
        {
            return {};
        }
        return component->wrapMode;
    }

    void Text3D::SetWrapMode(Component::TextWrapMode value)
    {
        Component::Text3D* component = Resolve("SetWrapMode");
        if (component == nullptr)
        {
            return;
        }
        component->wrapMode = value;
    }

    Component::TextAlignX Text3D::GetAlignX() const
    {
        const Component::Text3D* component = Resolve("GetAlignX");
        if (component == nullptr)
        {
            return {};
        }
        return component->alignX;
    }

    void Text3D::SetAlignX(Component::TextAlignX value)
    {
        Component::Text3D* component = Resolve("SetAlignX");
        if (component == nullptr)
        {
            return;
        }
        component->alignX = value;
    }

    Component::TextAlignY Text3D::GetAlignY() const
    {
        const Component::Text3D* component = Resolve("GetAlignY");
        if (component == nullptr)
        {
            return {};
        }
        return component->alignY;
    }

    void Text3D::SetAlignY(Component::TextAlignY value)
    {
        Component::Text3D* component = Resolve("SetAlignY");
        if (component == nullptr)
        {
            return;
        }
        component->alignY = value;
    }

    Float Text3D::GetLineSpacing() const
    {
        const Component::Text3D* component = Resolve("GetLineSpacing");
        if (component == nullptr)
        {
            return {};
        }
        return component->lineSpacing;
    }

    void Text3D::SetLineSpacing(Float value)
    {
        Component::Text3D* component = Resolve("SetLineSpacing");
        if (component == nullptr)
        {
            return;
        }
        component->lineSpacing = value;
    }

    Float Text3D::GetLetterSpacing() const
    {
        const Component::Text3D* component = Resolve("GetLetterSpacing");
        if (component == nullptr)
        {
            return {};
        }
        return component->letterSpacing;
    }

    void Text3D::SetLetterSpacing(Float value)
    {
        Component::Text3D* component = Resolve("SetLetterSpacing");
        if (component == nullptr)
        {
            return;
        }
        component->letterSpacing = value;
    }

    Color Text3D::GetColor() const
    {
        const Component::Text3D* component = Resolve("GetColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->color;
    }

    void Text3D::SetColor(Color value)
    {
        Component::Text3D* component = Resolve("SetColor");
        if (component == nullptr)
        {
            return;
        }
        component->color = value;
    }

    Bool Text3D::GetVisible() const
    {
        const Component::Text3D* component = Resolve("GetVisible");
        if (component == nullptr)
        {
            return {};
        }
        return component->visible;
    }

    void Text3D::SetVisible(Bool value)
    {
        Component::Text3D* component = Resolve("SetVisible");
        if (component == nullptr)
        {
            return;
        }
        component->visible = value;
    }

    Component::TextFacing3D Text3D::GetFacing() const
    {
        const Component::Text3D* component = Resolve("GetFacing");
        if (component == nullptr)
        {
            return {};
        }
        return component->facing;
    }

    void Text3D::SetFacing(Component::TextFacing3D value)
    {
        Component::Text3D* component = Resolve("SetFacing");
        if (component == nullptr)
        {
            return;
        }
        component->facing = value;
    }

    Color Text3D::GetOutlineColor() const
    {
        const Component::Text3D* component = Resolve("GetOutlineColor");
        if (component == nullptr)
        {
            return {};
        }
        return component->outlineColor;
    }

    void Text3D::SetOutlineColor(Color value)
    {
        Component::Text3D* component = Resolve("SetOutlineColor");
        if (component == nullptr)
        {
            return;
        }
        component->outlineColor = value;
    }

    Float Text3D::GetOutlineWidth() const
    {
        const Component::Text3D* component = Resolve("GetOutlineWidth");
        if (component == nullptr)
        {
            return {};
        }
        return component->outlineWidth;
    }

    void Text3D::SetOutlineWidth(Float value)
    {
        Component::Text3D* component = Resolve("SetOutlineWidth");
        if (component == nullptr)
        {
            return;
        }
        component->outlineWidth = value;
    }

    Bool Text3D::GetRichText() const
    {
        const Component::Text3D* component = Resolve("GetRichText");
        if (component == nullptr)
        {
            return {};
        }
        return component->richText;
    }

    void Text3D::SetRichText(Bool value)
    {
        Component::Text3D* component = Resolve("SetRichText");
        if (component == nullptr)
        {
            return;
        }
        component->richText = value;
    }
}
