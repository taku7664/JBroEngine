#include <JBro/Framework2D/Handle/AspectRatioFitter2D.h>

#include <JBro/Framework2D/Component/AspectRatioFitter2D.h>

namespace JBro::Handle
{
    Component::AspectRatioFitter2D* AspectRatioFitter2D::Resolve(const char* operation) const
    {
        return static_cast<Component::AspectRatioFitter2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_ASPECTRATIOFITTER2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type AspectRatioFitter2D::Getter() const                                   \
    {                                                               \
        const Component::AspectRatioFitter2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void AspectRatioFitter2D::Setter(Type value)                               \
    {                                                               \
        Component::AspectRatioFitter2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_ASPECTRATIOFITTER2D_ACCESSOR(Component::AspectMode, GetMode, SetMode, mode)
    JBRO_ASPECTRATIOFITTER2D_ACCESSOR(Float, GetAspect, SetAspect, aspect)

#undef JBRO_ASPECTRATIOFITTER2D_ACCESSOR
}
