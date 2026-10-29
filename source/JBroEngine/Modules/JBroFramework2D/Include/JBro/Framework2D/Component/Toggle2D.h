#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Toggle2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Toggle2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **켜고 끄는 단추**다(D-351, ui-plan §6.2 2 단계). 누름 사각형은 버튼과 같은 뜻이고(`offset` 가운데의 `size`), 위에서 누르고 떼면 `isOn` 이 뒤집힌다.
    // `checkmark` 는 켜졌을 때만 보이는 스프라이트다 - 시스템이 매 프레임 `visible` 을 `isOn` 으로 둔다. 비워도 된다.
    // 같은 오브젝트에 `Button2D` 를 두지 않는다 - 사각형이 둘이 된다. 포인터는 `Button2DSystem` 이 버튼과 같은 `"UI"` 체인에서 본다.
    // 같은 오브젝트의 스크립트는 뒤집힌 프레임에 `OnValueChanged` 를 받는다(포인터가 바꾼 것만).
    class Toggle2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Toggle2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Toggle2D)

        JBRO_FIELD(Vector2, size) { 24.0f, 24.0f };
        JBRO_FIELD(Vector2, offset);
        JBRO_FIELD(Bool, interactable) = true;
        JBRO_FIELD(Bool, isOn) = false;
        JBRO_FIELD(Handle::SpriteRenderer2D, checkmark);

        // 시스템이 채우는 이번 프레임의 상태다. `changed` 는 포인터가 뒤집은 그 프레임에만 참이다.
        JBRO_FIELD(Bool, hovered, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, pressed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, changed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, focused, NoSerialize() | ReadOnly() | Category("State")) = false;
    };
}
