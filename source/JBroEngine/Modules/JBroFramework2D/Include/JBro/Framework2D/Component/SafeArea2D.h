#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`SafeArea2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::SafeArea2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **안전 영역**이다(D-358, ui-plan §7.2·§7.3). 화면 레이어의 루트 오브젝트에 두면 앵커(`Transform2D.anchor`)를 화면 전체가 아니라 **안전 영역**(노치·홈 표시줄을 뺀
    // 안쪽, `ScreenSpaceFrame.safeArea`)의 사각형에서 잰다. 가로·세로를 따로 끌 수 있다. 데스크톱은 띠가 0 이라 화면 전체와 같다. 루트가 아닌 오브젝트에서는 아무 일도 없다.
    class SafeArea2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::SafeArea2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(SafeArea2D)

        JBRO_FIELD(Bool, horizontal) = true;
        JBRO_FIELD(Bool, vertical) = true;
    };
}
