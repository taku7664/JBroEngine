#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Dropdown2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Dropdown2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Framework2D/Handle/Text2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Component
{
    // **드롭다운**이다(D-355, ui-plan §7.2). 누름 사각형은 버튼과 같고(`offset` 가운데의 `size`), 누르면 `list`(**자식 오브젝트**)를 펴고 다시 누르거나 밖을 누르거나
    // Esc 면 접는다. 항목은 `list` 아래의 `Toggle2D` 들(계층 차례)이다 - 하나를 누르면 `value` 가 그 번호가 되고 접힌다. 시스템이 매 프레임 `list` 의 켜짐을 `open` 으로,
    // 항목 토글의 `isOn` 을 `value` 로 둔다. `caption` 텍스트에는 고른 항목 오브젝트의 `Text2D` 글자를 옮긴다(없으면 비운다).
    // 전이는 버튼과 같다. 같은 오브젝트의 스크립트는 값이 바뀐 프레임에 `OnValueChanged` 를 받는다. `value`·`open` 은 스크립트가 써도 된다.
    class Dropdown2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Dropdown2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Dropdown2D)

        JBRO_FIELD(Vector2, size, Pixels()) { 160.0f, 32.0f };
        JBRO_FIELD(Vector2, offset, Pixels());
        JBRO_FIELD(Bool, interactable) = true;
        // 전이(D-355): 버튼과 같다 - 상태(보통·호버·눌림·꺼짐)를 대상 그림에 보인다. `targetGraphic` 이 비면 같은 오브젝트의 스프라이트다.
        JBRO_FIELD(ButtonTransition, transition, Category("Transition")) = ButtonTransition::ColorTint;
        JBRO_FIELD(Handle::SpriteRenderer2D, targetGraphic, Tooltip("전이를 거는 스프라이트. 비면 같은 오브젝트의 SpriteRenderer2D 다") | Category("Transition"));
        JBRO_FIELD(Color, normalTint, Category("Transition")) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Color, hoverTint, Category("Transition")) { 0.9f, 0.9f, 0.9f, 1.0f };
        JBRO_FIELD(Color, pressedTint, Category("Transition")) { 0.7f, 0.7f, 0.7f, 1.0f };
        JBRO_FIELD(Color, disabledTint, Category("Transition")) { 0.5f, 0.5f, 0.5f, 0.6f };
        JBRO_FIELD(AssetId, hoverSpriteId, Category("Transition"));
        JBRO_FIELD(AssetHandle, hoverSprite, NoSerialize() | ReadOnly() | Tooltip("hoverSpriteId 에서 해석된 값") | Hidden());
        JBRO_FIELD(AssetId, pressedSpriteId, Category("Transition"));
        JBRO_FIELD(AssetHandle, pressedSprite, NoSerialize() | ReadOnly() | Tooltip("pressedSpriteId 에서 해석된 값") | Hidden());
        JBRO_FIELD(AssetId, disabledSpriteId, Category("Transition"));
        JBRO_FIELD(AssetHandle, disabledSprite, NoSerialize() | ReadOnly() | Tooltip("disabledSpriteId 에서 해석된 값") | Hidden());
        JBRO_FIELD(Handle::GameObject, list, Tooltip("펼칠 목록 오브젝트. 이 오브젝트의 자식이어야 한다 - 항목은 그 아래의 Toggle2D 다"));
        JBRO_FIELD(Handle::Text2D, caption, Tooltip("고른 항목의 글자를 보이는 텍스트"));
        // 고른 항목의 번호(계층 차례)다. -1 은 고른 것이 없다.
        JBRO_FIELD(Int32, value) = -1;

        // 시스템이 채우는 이번 프레임의 상태다. `open` 은 스크립트가 써도 된다. `changed` 는 값이 바뀐 그 프레임에만 참이다.
        JBRO_FIELD(Bool, open, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, hovered, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, pressed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, focused, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, changed, NoSerialize() | ReadOnly() | Category("State")) = false;
        // 마지막으로 캡션에 옮긴 값과 그 글자의 판번호다.
        JBRO_FIELD(Int32, shownValue, NoSerialize() | ReadOnly() | Hidden()) = -2;
        JBRO_FIELD(UInt32, shownRevision, NoSerialize() | ReadOnly() | Hidden()) = 0;
        // `SpriteSwap` 이 바꾸기 전에 떠 둔 보통 스프라이트와 바꿨는가(D-355, 버튼과 같다).
        JBRO_FIELD(AssetHandle, normalSprite, NoSerialize() | ReadOnly() | Hidden());
        JBRO_FIELD(Bool, swapped, NoSerialize() | ReadOnly() | Hidden()) = false;
    };
}
