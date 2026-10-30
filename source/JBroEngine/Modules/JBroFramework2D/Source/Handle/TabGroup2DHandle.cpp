#include <JBro/Framework2D/Handle/TabGroup2D.h>

#include <JBro/Framework2D/Component/TabGroup2D.h>

namespace JBro::Handle
{
    Component::TabGroup2D* TabGroup2D::Resolve(const char* operation) const
    {
        return static_cast<Component::TabGroup2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_TABGROUP2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type TabGroup2D::Getter() const                                   \
    {                                                               \
        const Component::TabGroup2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void TabGroup2D::Setter(Type value)                               \
    {                                                               \
        Component::TabGroup2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_TABGROUP2D_ACCESSOR(GameObject, GetContent, SetContent, content)

#undef JBRO_TABGROUP2D_ACCESSOR

    Int32 TabGroup2D::GetPage() const
    {
        const Component::TabGroup2D* component = Resolve("GetPage");
        return component != nullptr ? component->page : Int32(-1);
    }
}
