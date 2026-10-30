#include <JBro/Framework2D/Handle/ListView2D.h>

#include <JBro/Framework2D/Component/ListView2D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

namespace JBro::Handle
{
    Component::ListView2D* ListView2D::Resolve(const char* operation) const
    {
        return static_cast<Component::ListView2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_LISTVIEW2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type ListView2D::Getter() const                                   \
    {                                                               \
        const Component::ListView2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void ListView2D::Setter(Type value)                               \
    {                                                               \
        Component::ListView2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_LISTVIEW2D_ACCESSOR(Int32, GetCount, SetCount, count)
    JBRO_LISTVIEW2D_ACCESSOR(Vector2, GetItemSize, SetItemSize, itemSize)
    JBRO_LISTVIEW2D_ACCESSOR(Float, GetSpacing, SetSpacing, spacing)
    JBRO_LISTVIEW2D_ACCESSOR(Bool, GetVertical, SetVertical, vertical)
    JBRO_LISTVIEW2D_ACCESSOR(Bool, GetVariableSize, SetVariableSize, variableSize)
    JBRO_LISTVIEW2D_ACCESSOR(Float, GetMeasuredSize, SetMeasuredSize, measuredSize)

#undef JBRO_LISTVIEW2D_ACCESSOR

    void ListView2D::Remeasure()
    {
        Component::ListView2D* component = Resolve("Remeasure");
        if (component != nullptr)
        {
            component->remeasure = true;
        }
    }

    Int32 ListView2D::GetFirstVisible() const
    {
        const Component::ListView2D* component = Resolve("GetFirstVisible");
        return component != nullptr ? component->firstVisible : Int32(-1);
    }

    Int32 ListView2D::GetVisibleCount() const
    {
        const Component::ListView2D* component = Resolve("GetVisibleCount");
        return component != nullptr ? component->visibleCount : Int32(0);
    }

    GameObject ListView2D::GetBoundItem() const
    {
        const Component::ListView2D* component = Resolve("GetBoundItem");
        return component != nullptr ? component->boundItem : GameObject{};
    }

    Int32 ListView2D::GetBoundIndex() const
    {
        const Component::ListView2D* component = Resolve("GetBoundIndex");
        return component != nullptr ? component->boundIndex : Int32(-1);
    }

    Int32 ListView2D::GetItemIndex(GameObject item) const
    {
        const Component::ListView2D* component = Resolve("GetItemIndex");
        Object::GameObject* object = Internal::GameObjectHandleAccess::Resolve(item);
        const Object::GameObject* parent = object != nullptr ? object->GetParent() : nullptr;
        if (component == nullptr || object == nullptr || parent == nullptr)
        {
            return -1;
        }
        // 풀의 몇 번째인가 - 내용 오브젝트의 자식 차례다. 지금 범위로 센다.
        Int32 slot = 0;
        const Int32 pool = Int32(static_cast<std::int32_t>(parent->GetChildren().Size()));
        for (const SafePtr<Object::GameObject>& child : parent->GetChildren())
        {
            if (child.TryGet() == object)
            {
                return Component::ListView2D::SlotIndex(component->firstVisible, component->visibleCount, pool, slot);
            }
            ++slot;
        }
        return -1;
    }
}
