#include <JBro/Framework2D/Handle/ToggleGroup2D.h>

#include <JBro/Framework2D/Component/ToggleGroup2D.h>

namespace JBro::Handle
{
    Component::ToggleGroup2D* ToggleGroup2D::Resolve(const char* operation) const
    {
        return static_cast<Component::ToggleGroup2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_TOGGLEGROUP2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type ToggleGroup2D::Getter() const                                   \
    {                                                               \
        const Component::ToggleGroup2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void ToggleGroup2D::Setter(Type value)                               \
    {                                                               \
        Component::ToggleGroup2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_TOGGLEGROUP2D_ACCESSOR(Bool, GetAllowOff, SetAllowOff, allowOff)

#undef JBRO_TOGGLEGROUP2D_ACCESSOR

    Int32 ToggleGroup2D::GetValue() const
    {
        const Component::ToggleGroup2D* component = Resolve("GetValue");
        return component != nullptr ? component->value : Int32(-1);
    }
}
