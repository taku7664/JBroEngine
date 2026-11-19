#include <JBro/Framework2D/Handle/Modal2D.h>

#include <JBro/Framework2D/Component/Modal2D.h>

namespace JBro::Handle
{
    Component::Modal2D* Modal2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Modal2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_MODAL2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Modal2D::Getter() const                                   \
    {                                                               \
        const Component::Modal2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Modal2D::Setter(Type value)                               \
    {                                                               \
        Component::Modal2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_MODAL2D_ACCESSOR(Bool, GetBlockGame, SetBlockGame, blockGame)

#undef JBRO_MODAL2D_ACCESSOR
}
