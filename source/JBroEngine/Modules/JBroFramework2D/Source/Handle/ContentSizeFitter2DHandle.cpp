#include <JBro/Framework2D/Handle/ContentSizeFitter2D.h>

#include <JBro/Framework2D/Component/ContentSizeFitter2D.h>

namespace JBro::Handle
{
    Component::ContentSizeFitter2D* ContentSizeFitter2D::Resolve(const char* operation) const
    {
        return static_cast<Component::ContentSizeFitter2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_CONTENTSIZEFITTER2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type ContentSizeFitter2D::Getter() const                                   \
    {                                                               \
        const Component::ContentSizeFitter2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void ContentSizeFitter2D::Setter(Type value)                               \
    {                                                               \
        Component::ContentSizeFitter2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_CONTENTSIZEFITTER2D_ACCESSOR(Bool, GetFitWidth, SetFitWidth, fitWidth)
    JBRO_CONTENTSIZEFITTER2D_ACCESSOR(Bool, GetFitHeight, SetFitHeight, fitHeight)
    JBRO_CONTENTSIZEFITTER2D_ACCESSOR(Vector2, GetPadding, SetPadding, padding)
    JBRO_CONTENTSIZEFITTER2D_ACCESSOR(Vector2, GetMinSize, SetMinSize, minSize)

#undef JBRO_CONTENTSIZEFITTER2D_ACCESSOR

    Float ContentSizeFitter2D::GetContentWidth() const
    {
        const Component::ContentSizeFitter2D* component = Resolve("GetContentWidth");
        return component != nullptr ? component->contentWidth : Float(0.0f);
    }

    Float ContentSizeFitter2D::GetContentHeight() const
    {
        const Component::ContentSizeFitter2D* component = Resolve("GetContentHeight");
        return component != nullptr ? component->contentHeight : Float(0.0f);
    }
}
