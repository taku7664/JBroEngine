#include <JBro/Framework2D/Handle/SafeArea2D.h>

#include <JBro/Framework2D/Component/SafeArea2D.h>

namespace JBro::Handle
{
    Component::SafeArea2D* SafeArea2D::Resolve(const char* operation) const
    {
        return static_cast<Component::SafeArea2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_SAFEAREA2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type SafeArea2D::Getter() const                                   \
    {                                                               \
        const Component::SafeArea2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void SafeArea2D::Setter(Type value)                               \
    {                                                               \
        Component::SafeArea2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_SAFEAREA2D_ACCESSOR(Bool, GetHorizontal, SetHorizontal, horizontal)
    JBRO_SAFEAREA2D_ACCESSOR(Bool, GetVertical, SetVertical, vertical)

#undef JBRO_SAFEAREA2D_ACCESSOR
}
