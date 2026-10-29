#include <JBro/Framework2D/Handle/Button2D.h>

#include <JBro/Framework2D/Component/Button2D.h>

namespace JBro::Handle
{
    Component::Button2D* Button2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Button2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_BUTTON2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Button2D::Getter() const                                   \
    {                                                               \
        const Component::Button2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Button2D::Setter(Type value)                               \
    {                                                               \
        Component::Button2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_BUTTON2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_BUTTON2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_BUTTON2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)
    JBRO_BUTTON2D_ACCESSOR(Component::ButtonTransition, GetTransition, SetTransition, transition)
    JBRO_BUTTON2D_ACCESSOR(SpriteRenderer2D, GetTargetGraphic, SetTargetGraphic, targetGraphic)
    JBRO_BUTTON2D_ACCESSOR(Color, GetNormalTint, SetNormalTint, normalTint)
    JBRO_BUTTON2D_ACCESSOR(Color, GetHoverTint, SetHoverTint, hoverTint)
    JBRO_BUTTON2D_ACCESSOR(Color, GetPressedTint, SetPressedTint, pressedTint)
    JBRO_BUTTON2D_ACCESSOR(Color, GetDisabledTint, SetDisabledTint, disabledTint)
    JBRO_BUTTON2D_ACCESSOR(AssetHandle, GetHoverSprite, SetHoverSprite, hoverSprite)
    JBRO_BUTTON2D_ACCESSOR(AssetHandle, GetPressedSprite, SetPressedSprite, pressedSprite)
    JBRO_BUTTON2D_ACCESSOR(AssetHandle, GetDisabledSprite, SetDisabledSprite, disabledSprite)

#undef JBRO_BUTTON2D_ACCESSOR

    Bool Button2D::GetFocused() const
    {
        const Component::Button2D* component = Resolve("GetFocused");
        return component != nullptr ? component->focused : Bool(false);
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
