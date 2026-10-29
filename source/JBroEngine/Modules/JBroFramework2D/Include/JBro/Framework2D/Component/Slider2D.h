#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Slider2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Slider2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **끌어서 값을 고르는 막대**다(D-351, ui-plan §6.2 2 단계). 누름 사각형은 버튼과 같은 뜻이다(`offset` 가운데의 `size`, 로컬 좌표).
    // `value` 는 0..1 이고 `direction` 이 어느 변이 0 인지 정한다. 누르면 그 자리의 값으로 뛰고, 쥔 채 끌면 따라오며, 떼면 끝난다.
    //
    // `handle` 은 **자식 오브젝트**다 - 시스템이 매 프레임 값에서 그 로컬 자리를 사각형의 축 위에 놓는다(다른 축은 `offset` 의 것).
    // `fill` 은 `Filled` 스프라이트다 - `fillAmount` 가 값이 된다. 둘 다 비워도 된다. 값은 스크립트가 써도 되고, 그러면 손잡이·채우기가 따라온다.
    // 포인터는 `Button2DSystem` 이 버튼과 같은 `"UI"` 체인에서 본다 - 한 프레임에 가장 위 하나다. 같은 오브젝트의 스크립트는 값이 바뀐 프레임에
    // `OnValueChanged` 를 받는다(포인터가 바꾼 것만 - 스크립트가 쓴 값은 알리지 않는다).
    class Slider2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Slider2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Slider2D)

        JBRO_FIELD(Vector2, size) { 160.0f, 24.0f };
        JBRO_FIELD(Vector2, offset);
        JBRO_FIELD(Bool, interactable) = true;
        JBRO_FIELD(SliderDirection, direction) = SliderDirection::LeftToRight;
        JBRO_FIELD(Float, value, Range(0.0f, 1.0f)) = 0.0f;
        JBRO_FIELD(Handle::GameObject, handle);
        JBRO_FIELD(Handle::SpriteRenderer2D, fill);

        // 시스템이 채우는 이번 프레임의 상태다. `changed` 는 포인터가 값을 바꾼 그 프레임에만 참이다.
        JBRO_FIELD(Bool, hovered, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, pressed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, changed, NoSerialize() | ReadOnly() | Category("State")) = false;
    };
}
