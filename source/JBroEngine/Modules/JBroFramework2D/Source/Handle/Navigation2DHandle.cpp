#include <JBro/Framework2D/Handle/Navigation2D.h>

#include <JBro/Framework2D/Component/Navigation2D.h>

namespace JBro::Handle
{
    Component::Navigation2D* Navigation2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Navigation2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_NAVIGATION2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Navigation2D::Getter() const                                   \
    {                                                               \
        const Component::Navigation2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Navigation2D::Setter(Type value)                               \
    {                                                               \
        Component::Navigation2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_NAVIGATION2D_ACCESSOR(GameObject, GetUp, SetUp, up)
    JBRO_NAVIGATION2D_ACCESSOR(GameObject, GetDown, SetDown, down)
    JBRO_NAVIGATION2D_ACCESSOR(GameObject, GetLeft, SetLeft, left)
    JBRO_NAVIGATION2D_ACCESSOR(GameObject, GetRight, SetRight, right)

#undef JBRO_NAVIGATION2D_ACCESSOR
}
