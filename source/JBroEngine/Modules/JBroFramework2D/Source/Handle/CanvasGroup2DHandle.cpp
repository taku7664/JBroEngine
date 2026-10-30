#include <JBro/Framework2D/Handle/CanvasGroup2D.h>

#include <JBro/Framework2D/Component/CanvasGroup2D.h>

namespace JBro::Handle
{
    Component::CanvasGroup2D* CanvasGroup2D::Resolve(const char* operation) const
    {
        return static_cast<Component::CanvasGroup2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_CANVASGROUP2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type CanvasGroup2D::Getter() const                                   \
    {                                                               \
        const Component::CanvasGroup2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void CanvasGroup2D::Setter(Type value)                               \
    {                                                               \
        Component::CanvasGroup2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_CANVASGROUP2D_ACCESSOR(Float, GetAlpha, SetAlpha, alpha)
    JBRO_CANVASGROUP2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)

#undef JBRO_CANVASGROUP2D_ACCESSOR
}
