#include <JBro/Framework2D/Handle/ScrollView2D.h>

#include <JBro/Framework2D/Component/ScrollView2D.h>

namespace JBro::Handle
{
    Component::ScrollView2D* ScrollView2D::Resolve(const char* operation) const
    {
        return static_cast<Component::ScrollView2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_SCROLLVIEW2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type ScrollView2D::Getter() const                                   \
    {                                                                   \
        const Component::ScrollView2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                       \
        {                                                               \
            return {};                                                  \
        }                                                               \
        return component->field;                                        \
    }                                                                   \
    void ScrollView2D::Setter(Type value)                               \
    {                                                                   \
        Component::ScrollView2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                       \
        {                                                               \
            return;                                                     \
        }                                                               \
        component->field = value;                                       \
    }

    JBRO_SCROLLVIEW2D_ACCESSOR(GameObject, GetContent, SetContent, content)
    JBRO_SCROLLVIEW2D_ACCESSOR(Vector2, GetContentSize, SetContentSize, contentSize)
    JBRO_SCROLLVIEW2D_ACCESSOR(Bool, GetHorizontal, SetHorizontal, horizontal)
    JBRO_SCROLLVIEW2D_ACCESSOR(Bool, GetVertical, SetVertical, vertical)
    JBRO_SCROLLVIEW2D_ACCESSOR(Float, GetScrollSensitivity, SetScrollSensitivity, scrollSensitivity)
    JBRO_SCROLLVIEW2D_ACCESSOR(Float, GetDecelerationRate, SetDecelerationRate, decelerationRate)

#undef JBRO_SCROLLVIEW2D_ACCESSOR

    Bool ScrollView2D::GetDragging() const
    {
        const Component::ScrollView2D* component = Resolve("GetDragging");
        return component != nullptr ? component->dragging : Bool(false);
    }

    Vector2 ScrollView2D::GetVelocity() const
    {
        const Component::ScrollView2D* component = Resolve("GetVelocity");
        return component != nullptr ? component->velocity : Vector2{};
    }
}
