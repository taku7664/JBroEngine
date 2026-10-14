#include <JBro/Framework2D/Handle/Button2D.h>

#include <JBro/Framework2D/Component/Button2D.h>

namespace JBro::Handle
{
    Component::Button2D* Button2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Button2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Vector2 Button2D::GetSize() const
    {
        const Component::Button2D* component = Resolve("GetSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->size;
    }

    void Button2D::SetSize(Vector2 value)
    {
        Component::Button2D* component = Resolve("SetSize");
        if (component == nullptr)
        {
            return;
        }
        component->size = value;
    }

    Vector2 Button2D::GetOffset() const
    {
        const Component::Button2D* component = Resolve("GetOffset");
        if (component == nullptr)
        {
            return {};
        }
        return component->offset;
    }

    void Button2D::SetOffset(Vector2 value)
    {
        Component::Button2D* component = Resolve("SetOffset");
        if (component == nullptr)
        {
            return;
        }
        component->offset = value;
    }

    Bool Button2D::GetInteractable() const
    {
        const Component::Button2D* component = Resolve("GetInteractable");
        if (component == nullptr)
        {
            return {};
        }
        return component->interactable;
    }

    void Button2D::SetInteractable(Bool value)
    {
        Component::Button2D* component = Resolve("SetInteractable");
        if (component == nullptr)
        {
            return;
        }
        component->interactable = value;
    }

    Bool Button2D::GetTintSprite() const
    {
        const Component::Button2D* component = Resolve("GetTintSprite");
        if (component == nullptr)
        {
            return {};
        }
        return component->tintSprite;
    }

    void Button2D::SetTintSprite(Bool value)
    {
        Component::Button2D* component = Resolve("SetTintSprite");
        if (component == nullptr)
        {
            return;
        }
        component->tintSprite = value;
    }

    Color Button2D::GetNormalTint() const
    {
        const Component::Button2D* component = Resolve("GetNormalTint");
        if (component == nullptr)
        {
            return {};
        }
        return component->normalTint;
    }

    void Button2D::SetNormalTint(Color value)
    {
        Component::Button2D* component = Resolve("SetNormalTint");
        if (component == nullptr)
        {
            return;
        }
        component->normalTint = value;
    }

    Color Button2D::GetHoverTint() const
    {
        const Component::Button2D* component = Resolve("GetHoverTint");
        if (component == nullptr)
        {
            return {};
        }
        return component->hoverTint;
    }

    void Button2D::SetHoverTint(Color value)
    {
        Component::Button2D* component = Resolve("SetHoverTint");
        if (component == nullptr)
        {
            return;
        }
        component->hoverTint = value;
    }

    Color Button2D::GetPressedTint() const
    {
        const Component::Button2D* component = Resolve("GetPressedTint");
        if (component == nullptr)
        {
            return {};
        }
        return component->pressedTint;
    }

    void Button2D::SetPressedTint(Color value)
    {
        Component::Button2D* component = Resolve("SetPressedTint");
        if (component == nullptr)
        {
            return;
        }
        component->pressedTint = value;
    }

    Color Button2D::GetDisabledTint() const
    {
        const Component::Button2D* component = Resolve("GetDisabledTint");
        if (component == nullptr)
        {
            return {};
        }
        return component->disabledTint;
    }

    void Button2D::SetDisabledTint(Color value)
    {
        Component::Button2D* component = Resolve("SetDisabledTint");
        if (component == nullptr)
        {
            return;
        }
        component->disabledTint = value;
    }

    Bool Button2D::GetHovered() const
    {
        const Component::Button2D* component = Resolve("GetHovered");
        if (component == nullptr)
        {
            return {};
        }
        return component->hovered;
    }

    Bool Button2D::GetPressed() const
    {
        const Component::Button2D* component = Resolve("GetPressed");
        if (component == nullptr)
        {
            return {};
        }
        return component->pressed;
    }

    Bool Button2D::GetClicked() const
    {
        const Component::Button2D* component = Resolve("GetClicked");
        if (component == nullptr)
        {
            return {};
        }
        return component->clicked;
    }
}
