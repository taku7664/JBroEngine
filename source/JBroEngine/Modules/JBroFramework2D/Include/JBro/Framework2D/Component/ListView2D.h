#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`ListView2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::ListView2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::Component
{
    // **가상화 목록**이다(D-357, ui-plan §7.2). 같은 오브젝트의 `ScrollView2D`(와 `Mask2D`)의 내용 아래에 사용자가 만들어 둔 항목 오브젝트들(풀, 보이는 수 + 1 이상)을
    // 돌려 쓴다 - `count` 개 가운데 뷰포트에 걸치는 것만 풀의 오브젝트에 묶어 제 자리에 놓고 켜며, 나머지 풀은 끈다. 항목 `i` 는 풀의 `i % 풀 크기` 번째다. 묶임이 바뀐
    // 항목마다 이 오브젝트의 스크립트가 `OnListItem` 을 받는다 - 그때 `boundItem`·`boundIndex` 가 그 오브젝트와 번호다. 스크롤 뷰의 `contentSize` 는 여기서 쓴다.
    // 내용에 `Layout2D` 를 두지 않는다(자리가 둘이 된다). 항목의 크기·간격은 고정이다. `Layout2DSystem` 이 돈다.
    class ListView2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ListView2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(ListView2D)

        JBRO_FIELD(Int32, count, Range(0.0f, 1000000.0f)) = 0;
        JBRO_FIELD(Vector2, itemSize, Pixels()) { 100.0f, 40.0f };
        JBRO_FIELD(Float, spacing, Pixels()) = 4.0f;
        JBRO_FIELD(Bool, vertical) = true;

        // 시스템이 채우는 상태다. `boundItem`·`boundIndex` 는 `OnListItem` 을 부르는 동안 그 항목이다.
        JBRO_FIELD(Int32, firstVisible, NoSerialize() | ReadOnly() | Category("State")) = -1;
        JBRO_FIELD(Int32, visibleCount, NoSerialize() | ReadOnly() | Category("State")) = 0;
        JBRO_FIELD(Handle::GameObject, boundItem, NoSerialize() | ReadOnly() | Category("State"));
        JBRO_FIELD(Int32, boundIndex, NoSerialize() | ReadOnly() | Category("State")) = -1;
        // 지난 갱신의 범위와 풀 크기다 - 이것으로 풀의 칸마다 전에 묶였던 번호를 되센다(`SlotIndex`).
        JBRO_FIELD(Int32, shownFirst, NoSerialize() | ReadOnly() | Hidden()) = -1;
        JBRO_FIELD(Int32, shownVisible, NoSerialize() | ReadOnly() | Hidden()) = 0;
        JBRO_FIELD(Int32, shownPool, NoSerialize() | ReadOnly() | Hidden()) = 0;

        // 풀의 `slot` 번째 오브젝트에 묶이는 항목 번호다(-1 은 없음). 항목 `i` 는 풀의 `i % 풀 크기` 번째이고, 보이는 범위 `[first, first + visible)` 만 묶인다.
        static Int32 SlotIndex(Int32 first, Int32 visible, Int32 pool, Int32 slot)
        {
            if (visible <= 0 || pool <= 0 || slot < 0 || slot >= pool || first < 0)
            {
                return -1;
            }
            const Int32 offset = Int32(((slot - first % pool) + pool).Get() % pool.Get());
            return offset < visible ? Int32(first + offset) : Int32(-1);
        }
    };
}
