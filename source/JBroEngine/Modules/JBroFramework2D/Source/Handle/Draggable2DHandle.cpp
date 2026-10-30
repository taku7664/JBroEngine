#include <JBro/Framework2D/Handle/Draggable2D.h>

#include <JBro/Framework2D/Component/Draggable2D.h>

namespace JBro::Handle
{
    Component::Draggable2D* Draggable2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Draggable2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_DRAGGABLE2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Draggable2D::Getter() const                                   \
    {                                                               \
        const Component::Draggable2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Draggable2D::Setter(Type value)                               \
    {                                                               \
        Component::Draggable2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_DRAGGABLE2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_DRAGGABLE2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_DRAGGABLE2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)
    JBRO_DRAGGABLE2D_ACCESSOR(Bool, GetSnapBack, SetSnapBack, snapBack)

#undef JBRO_DRAGGABLE2D_ACCESSOR

    Bool Draggable2D::GetDragging() const
    {
        const Component::Draggable2D* component = Resolve("GetDragging");
        return component != nullptr ? component->dragging : Bool(false);
    }

    GameObject Draggable2D::GetDroppedOn() const
    {
        const Component::Draggable2D* component = Resolve("GetDroppedOn");
        return component != nullptr ? component->droppedOn : GameObject{};
    }
}
