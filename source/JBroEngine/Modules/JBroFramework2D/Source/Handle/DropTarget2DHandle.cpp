#include <JBro/Framework2D/Handle/DropTarget2D.h>

#include <JBro/Framework2D/Component/DropTarget2D.h>

namespace JBro::Handle
{
    Component::DropTarget2D* DropTarget2D::Resolve(const char* operation) const
    {
        return static_cast<Component::DropTarget2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_DROPTARGET2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type DropTarget2D::Getter() const                                   \
    {                                                               \
        const Component::DropTarget2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void DropTarget2D::Setter(Type value)                               \
    {                                                               \
        Component::DropTarget2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_DROPTARGET2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_DROPTARGET2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)

#undef JBRO_DROPTARGET2D_ACCESSOR

    Bool DropTarget2D::GetHovering() const
    {
        const Component::DropTarget2D* component = Resolve("GetHovering");
        return component != nullptr ? component->hovering : Bool(false);
    }

    GameObject DropTarget2D::GetDropped() const
    {
        const Component::DropTarget2D* component = Resolve("GetDropped");
        return component != nullptr ? component->dropped : GameObject{};
    }
}
