#include <JBro/Framework2D/Handle/Tooltip2D.h>

#include <JBro/Framework2D/Component/Tooltip2D.h>

namespace JBro::Handle
{
    Component::Tooltip2D* Tooltip2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Tooltip2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_TOOLTIP2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Tooltip2D::Getter() const                                   \
    {                                                               \
        const Component::Tooltip2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Tooltip2D::Setter(Type value)                               \
    {                                                               \
        Component::Tooltip2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_TOOLTIP2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_TOOLTIP2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_TOOLTIP2D_ACCESSOR(Float, GetDelay, SetDelay, delay)
    JBRO_TOOLTIP2D_ACCESSOR(Bool, GetFollow, SetFollow, follow)
    JBRO_TOOLTIP2D_ACCESSOR(Vector2, GetFollowOffset, SetFollowOffset, followOffset)
    JBRO_TOOLTIP2D_ACCESSOR(GameObject, GetTarget, SetTarget, target)

#undef JBRO_TOOLTIP2D_ACCESSOR

    Bool Tooltip2D::GetShown() const
    {
        const Component::Tooltip2D* component = Resolve("GetShown");
        return component != nullptr ? component->shown : Bool(false);
    }
}
