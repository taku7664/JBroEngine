#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`TabGroup2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::TabGroup2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Int.h>

namespace JBro::Component
{
    // **탭**이다(D-357, ui-plan §7.2). 같은 오브젝트의 `ToggleGroup2D` 가 고른 번호(`value`)와 짝인 페이지만 켠다 - 페이지는 `content` 오브젝트의 자식들(계층 차례)이다.
    // 그룹이 없거나 고른 것이 없으면 페이지를 모두 끈다. 페이지의 켜짐은 시스템의 것이다.
    class TabGroup2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::TabGroup2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(TabGroup2D)

        JBRO_FIELD(Handle::GameObject, content, Tooltip("페이지 오브젝트의 부모. 그 자식이 차례로 그룹의 토글과 짝이다"));

        // 시스템이 채우는 이번 프레임의 상태다 - 켜진 페이지의 번호(-1 은 없음).
        JBRO_FIELD(Int32, page, NoSerialize() | ReadOnly() | Category("State")) = -1;
    };
}
