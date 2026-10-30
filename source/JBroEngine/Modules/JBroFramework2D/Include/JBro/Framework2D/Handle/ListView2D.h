#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class ListView2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `ListView2D` 다(D-302·D-357). 가상화 목록이다. `OnListItem` 안에서 `GetBoundItem`·`GetBoundIndex` 가 묶이는 항목이고, `GetItemIndex` 는 풀의 오브젝트가
    // 지금 어느 번호인가다(-1 은 없음).
    class ListView2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ListView2D";
        }

        Int32 GetCount() const;
        void SetCount(Int32 value);
        Vector2 GetItemSize() const;
        void SetItemSize(Vector2 value);
        Float GetSpacing() const;
        void SetSpacing(Float value);
        Bool GetVertical() const;
        void SetVertical(Bool value);
        Int32 GetFirstVisible() const;
        Int32 GetVisibleCount() const;
        GameObject GetBoundItem() const;
        Int32 GetBoundIndex() const;
        Int32 GetItemIndex(GameObject item) const;

    private:
        Component::ListView2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(ListView2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<ListView2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<ListView2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(ListView2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::ListView2D);
