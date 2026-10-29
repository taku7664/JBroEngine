#include <JBro/Framework2D/Handle/Toggle2D.h>

#include <JBro/Framework2D/Component/Toggle2D.h>

namespace JBro::Handle
{
    Component::Toggle2D* Toggle2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Toggle2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_TOGGLE2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Toggle2D::Getter() const                                   \
    {                                                               \
        const Component::Toggle2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Toggle2D::Setter(Type value)                               \
    {                                                               \
        Component::Toggle2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_TOGGLE2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_TOGGLE2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_TOGGLE2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)
    JBRO_TOGGLE2D_ACCESSOR(Bool, GetIsOn, SetIsOn, isOn)
    JBRO_TOGGLE2D_ACCESSOR(SpriteRenderer2D, GetCheckmark, SetCheckmark, checkmark)

#undef JBRO_TOGGLE2D_ACCESSOR

    Bool Toggle2D::GetHovered() const
    {
        const Component::Toggle2D* component = Resolve("GetHovered");
        return component != nullptr ? component->hovered : Bool(false);
    }

    Bool Toggle2D::GetPressed() const
    {
        const Component::Toggle2D* component = Resolve("GetPressed");
        return component != nullptr ? component->pressed : Bool(false);
    }

    Bool Toggle2D::GetChanged() const
    {
        const Component::Toggle2D* component = Resolve("GetChanged");
        return component != nullptr ? component->changed : Bool(false);
    }
}
