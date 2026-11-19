#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Modal2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Modal2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **모달**이다(D-357, ui-plan §7.2). 켜져 있는 동안 이 오브젝트와 자손의 위젯만 포인터·포커스를 받고, 밖의 위젯은 없는 것이다(여럿이면 그 어느 것의 안이든 된다).
    // `blockGame` 이면 그동안 포인터·키보드·게임패드를 게임에도 주지 않는다. 그리기는 손대지 않는다 - 뒤를 가리는 막은 스프라이트로 둔다.
    class Modal2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Modal2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Modal2D)

        JBRO_FIELD(Bool, blockGame) = true;
    };
}
