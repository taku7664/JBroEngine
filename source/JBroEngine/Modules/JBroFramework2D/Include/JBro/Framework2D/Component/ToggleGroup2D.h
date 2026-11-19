#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`ToggleGroup2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::ToggleGroup2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace JBro::Component
{
    // **토글 그룹**이다(D-355, ui-plan §7.2). 이 오브젝트와 자손의 `Toggle2D` 가운데 **하나만 켜진다** - 하나가 켜지면 나머지는 꺼진다.
    // `allowOff` 가 거짓이면 켜진 것을 다시 눌러도 꺼지지 않는다(스크립트가 끄는 것은 막지 않는다). `value` 는 켜진 토글의 번호(계층 차례, -1 은 없음)다.
    // 스크립트가 둘 이상을 켜면 전에 켜져 있지 않던 쪽을 남긴다. 배타는 `Button2DSystem` 의 갱신 훅이 매 프레임 세운다.
    class ToggleGroup2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ToggleGroup2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(ToggleGroup2D)

        JBRO_FIELD(Bool, allowOff) = false;
        // 켜진 토글의 번호(계층 차례)다. 없으면 -1.
        JBRO_FIELD(Int32, value, NoSerialize() | ReadOnly() | Category("State")) = -1;
    };
}
