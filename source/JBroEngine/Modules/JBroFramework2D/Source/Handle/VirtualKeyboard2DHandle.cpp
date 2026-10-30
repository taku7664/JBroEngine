#include <JBro/Framework2D/Handle/VirtualKeyboard2D.h>

#include <JBro/Framework2D/Component/VirtualKeyboard2D.h>

namespace JBro::Handle
{
    Component::VirtualKeyboard2D* VirtualKeyboard2D::Resolve(const char* operation) const
    {
        return static_cast<Component::VirtualKeyboard2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_VIRTUALKEYBOARD2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type VirtualKeyboard2D::Getter() const                                   \
    {                                                               \
        const Component::VirtualKeyboard2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void VirtualKeyboard2D::Setter(Type value)                               \
    {                                                               \
        Component::VirtualKeyboard2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_VIRTUALKEYBOARD2D_ACCESSOR(GameObject, GetTarget, SetTarget, target)
    JBRO_VIRTUALKEYBOARD2D_ACCESSOR(GameObject, GetBackspace, SetBackspace, backspace)
    JBRO_VIRTUALKEYBOARD2D_ACCESSOR(GameObject, GetEnter, SetEnter, enter)
    JBRO_VIRTUALKEYBOARD2D_ACCESSOR(GameObject, GetSpace, SetSpace, space)

#undef JBRO_VIRTUALKEYBOARD2D_ACCESSOR
}
