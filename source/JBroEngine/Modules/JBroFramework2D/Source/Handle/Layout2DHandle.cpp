#include <JBro/Framework2D/Handle/Layout2D.h>

#include <JBro/Framework2D/Component/Layout2D.h>

namespace JBro::Handle
{
    Component::Layout2D* Layout2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Layout2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_LAYOUT2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Layout2D::Getter() const                                   \
    {                                                               \
        const Component::Layout2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Layout2D::Setter(Type value)                               \
    {                                                               \
        Component::Layout2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_LAYOUT2D_ACCESSOR(Component::LayoutMode, GetMode, SetMode, mode)
    JBRO_LAYOUT2D_ACCESSOR(Vector2, GetCellSize, SetCellSize, cellSize)
    JBRO_LAYOUT2D_ACCESSOR(Vector2, GetSpacing, SetSpacing, spacing)
    JBRO_LAYOUT2D_ACCESSOR(Vector2, GetPadding, SetPadding, padding)
    JBRO_LAYOUT2D_ACCESSOR(Int32, GetColumns, SetColumns, columns)
    JBRO_LAYOUT2D_ACCESSOR(Vector2, GetPivot, SetPivot, pivot)

    JBRO_LAYOUT2D_ACCESSOR(Bool, GetFitChildren, SetFitChildren, fitChildren)

#undef JBRO_LAYOUT2D_ACCESSOR

    Float Layout2D::GetContentWidth() const
    {
        const Component::Layout2D* component = Resolve("GetContentWidth");
        return component != nullptr ? component->contentWidth : Float(0.0f);
    }

    Float Layout2D::GetContentHeight() const
    {
        const Component::Layout2D* component = Resolve("GetContentHeight");
        return component != nullptr ? component->contentHeight : Float(0.0f);
    }
}
