#include <JBro/Framework2D/Handle/Dropdown2D.h>

#include <JBro/Framework2D/Component/Dropdown2D.h>

namespace JBro::Handle
{
    Component::Dropdown2D* Dropdown2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Dropdown2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_DROPDOWN2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Dropdown2D::Getter() const                                   \
    {                                                               \
        const Component::Dropdown2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Dropdown2D::Setter(Type value)                               \
    {                                                               \
        Component::Dropdown2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_DROPDOWN2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_DROPDOWN2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_DROPDOWN2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)
    JBRO_DROPDOWN2D_ACCESSOR(Component::ButtonTransition, GetTransition, SetTransition, transition)
    JBRO_DROPDOWN2D_ACCESSOR(SpriteRenderer2D, GetTargetGraphic, SetTargetGraphic, targetGraphic)
    JBRO_DROPDOWN2D_ACCESSOR(Color, GetNormalTint, SetNormalTint, normalTint)
    JBRO_DROPDOWN2D_ACCESSOR(Color, GetHoverTint, SetHoverTint, hoverTint)
    JBRO_DROPDOWN2D_ACCESSOR(Color, GetPressedTint, SetPressedTint, pressedTint)
    JBRO_DROPDOWN2D_ACCESSOR(Color, GetDisabledTint, SetDisabledTint, disabledTint)
    JBRO_DROPDOWN2D_ACCESSOR(AssetHandle, GetHoverSprite, SetHoverSprite, hoverSprite)
    JBRO_DROPDOWN2D_ACCESSOR(AssetHandle, GetPressedSprite, SetPressedSprite, pressedSprite)
    JBRO_DROPDOWN2D_ACCESSOR(AssetHandle, GetDisabledSprite, SetDisabledSprite, disabledSprite)
    JBRO_DROPDOWN2D_ACCESSOR(GameObject, GetList, SetList, list)
    JBRO_DROPDOWN2D_ACCESSOR(Text2D, GetCaption, SetCaption, caption)
    JBRO_DROPDOWN2D_ACCESSOR(Int32, GetValue, SetValue, value)
    JBRO_DROPDOWN2D_ACCESSOR(Bool, GetOpen, SetOpen, open)

#undef JBRO_DROPDOWN2D_ACCESSOR

    Bool Dropdown2D::GetHovered() const
    {
        const Component::Dropdown2D* component = Resolve("GetHovered");
        return component != nullptr ? component->hovered : Bool(false);
    }

    Bool Dropdown2D::GetPressed() const
    {
        const Component::Dropdown2D* component = Resolve("GetPressed");
        return component != nullptr ? component->pressed : Bool(false);
    }

    Bool Dropdown2D::GetFocused() const
    {
        const Component::Dropdown2D* component = Resolve("GetFocused");
        return component != nullptr ? component->focused : Bool(false);
    }

    Int32 Dropdown2D::GetHighlighted() const
    {
        const Component::Dropdown2D* component = Resolve("GetHighlighted");
        return component != nullptr ? component->highlighted : Int32(-1);
    }

    Bool Dropdown2D::GetChanged() const
    {
        const Component::Dropdown2D* component = Resolve("GetChanged");
        return component != nullptr ? component->changed : Bool(false);
    }
}
