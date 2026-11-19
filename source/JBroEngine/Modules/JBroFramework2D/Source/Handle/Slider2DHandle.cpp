#include <JBro/Framework2D/Handle/Slider2D.h>

#include <JBro/Framework2D/Component/Slider2D.h>

namespace JBro::Handle
{
    Component::Slider2D* Slider2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Slider2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_SLIDER2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Slider2D::Getter() const                                   \
    {                                                               \
        const Component::Slider2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Slider2D::Setter(Type value)                               \
    {                                                               \
        Component::Slider2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_SLIDER2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_SLIDER2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_SLIDER2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)
    JBRO_SLIDER2D_ACCESSOR(Component::SliderDirection, GetDirection, SetDirection, direction)
    JBRO_SLIDER2D_ACCESSOR(Float, GetValue, SetValue, value)
    JBRO_SLIDER2D_ACCESSOR(GameObject, GetHandle, SetHandle, handle)
    JBRO_SLIDER2D_ACCESSOR(SpriteRenderer2D, GetFill, SetFill, fill)

    JBRO_SLIDER2D_ACCESSOR(Component::ButtonTransition, GetTransition, SetTransition, transition)
    JBRO_SLIDER2D_ACCESSOR(SpriteRenderer2D, GetTargetGraphic, SetTargetGraphic, targetGraphic)
    JBRO_SLIDER2D_ACCESSOR(Color, GetNormalTint, SetNormalTint, normalTint)
    JBRO_SLIDER2D_ACCESSOR(Color, GetHoverTint, SetHoverTint, hoverTint)
    JBRO_SLIDER2D_ACCESSOR(Color, GetPressedTint, SetPressedTint, pressedTint)
    JBRO_SLIDER2D_ACCESSOR(Color, GetDisabledTint, SetDisabledTint, disabledTint)
    JBRO_SLIDER2D_ACCESSOR(AssetHandle, GetHoverSprite, SetHoverSprite, hoverSprite)
    JBRO_SLIDER2D_ACCESSOR(AssetHandle, GetPressedSprite, SetPressedSprite, pressedSprite)
    JBRO_SLIDER2D_ACCESSOR(AssetHandle, GetDisabledSprite, SetDisabledSprite, disabledSprite)
#undef JBRO_SLIDER2D_ACCESSOR

    Bool Slider2D::GetFocused() const
    {
        const Component::Slider2D* component = Resolve("GetFocused");
        return component != nullptr ? component->focused : Bool(false);
    }

    Bool Slider2D::GetHovered() const
    {
        const Component::Slider2D* component = Resolve("GetHovered");
        return component != nullptr ? component->hovered : Bool(false);
    }

    Bool Slider2D::GetPressed() const
    {
        const Component::Slider2D* component = Resolve("GetPressed");
        return component != nullptr ? component->pressed : Bool(false);
    }

    Bool Slider2D::GetChanged() const
    {
        const Component::Slider2D* component = Resolve("GetChanged");
        return component != nullptr ? component->changed : Bool(false);
    }
}
