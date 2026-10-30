#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`InputField2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::InputField2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Framework2D/Handle/Text2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Component
{
    // **글자를 받는 칸**이다(D-352, ui-plan §6.2 4 단계). 누름 사각형은 버튼과 같은 뜻이고, 위를 누르면 포커스를 받아 키보드의 글자가 `text` 에 붙는다
    // (다른 데를 누르거나 Esc 면 포커스를 잃는다). 글자는 `Text2D` 와 같이 호스트 `TextStore` 의 번호(`text`)이고 파일에는 글자로 적힌다.
    //
    // `display` 는 글자를 보이는 `Text2D` 다 - 시스템이 바뀔 때마다 그 글자를 덮어쓴다. `caret` 은 글자 끝에 서는 스프라이트다(`display` 의 자식이어야
    // 한다 - 그 로컬 자리에 놓는다). 포커스가 있을 때만 보이고 `blinkRate` 초마다 깜빡인다. 커서(`caretIndex`)는 포커스를 받으면 글자 끝이고
    // ←·→·Home·End 로 옮긴다(D-354) - 글자는 커서 자리에 끼고, Backspace 는 커서 앞 글자, Delete 는 커서 뒤 글자를 지운다. 쥐고 있는
    // Backspace·Delete·←·→ 는 지연 뒤 간격마다 되풀이한다. 누른 자리에 커서가 서고 끌면 선택이며(D-355), Shift+화살표·Home·End 가 선택을 늘리고
    // Ctrl+A/C/X/V 가 전부·복사·잘라내기·붙여넣기다(클립보드는 호스트의 것). 선택 구간은 `selection` 스프라이트(`display` 의 자식)를 두 커서 사이로 늘려 보인다.
    // IME 가 조합 중인 글자는 저장 글자에 넣지 않고 보이는 텍스트의 커서 자리에만 끼운다(`composing`).
    // Enter 는 `submitted`(한 프레임)와 `OnSubmit`, 바뀐 프레임은 `changed` 와 `OnValueChanged` 다.
    // 포커스가 있는 동안 키보드는 `"UI"` 체인에서 소비된다. `focused` 는 스크립트가 써도 된다 - 참이면 다음 프레임에 포커스를 받고(여럿이면 먼저 도는 것),
    // 거짓이면 놓는다. `caret` 이 `display` 의 자식이 아니면 놓지 않는다(보이기만 바뀐다).
    class InputField2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::InputField2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        // 떼이면 제 글자 칸을 돌려준다(`Text2D` 와 같다).
        void OnDetached() override;

        JBRO_REFLECT_BODY(InputField2D)

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
        JBRO_FIELD(TextId, text);
        JBRO_FIELD(Handle::Text2D, display, Tooltip("글자를 보이는 텍스트. 바뀔 때마다 그 글자를 덮어쓴다"));
        JBRO_FIELD(Handle::SpriteRenderer2D, caret, Tooltip("커서 스프라이트. display 의 자식이어야 한다 - 커서 자리에 놓는다"));
        JBRO_FIELD(Handle::SpriteRenderer2D, selection, Tooltip("선택 구간 스프라이트. display 의 자식이어야 한다 - 고른 글자 사이로 늘린다"));
        // 글자 수(코드포인트)의 상한이다. 0 이면 없다.
        JBRO_FIELD(Int32, maxLength) = 0;
        JBRO_FIELD(Float, blinkRate, Range(0.0f, 5.0f)) = 0.5f;

        // 시스템이 채우는 이번 프레임의 상태다. `focused` 는 스크립트가 써도 된다(핸들의 `SetFocused`) - 다음 프레임에 시스템이 받는다. 인스펙터에서는 읽기만이다.
        JBRO_FIELD(Bool, hovered, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, pressed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, focused, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, changed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, submitted, NoSerialize() | ReadOnly() | Category("State")) = false;
        // 커서 자리(코드포인트 번호, 0 은 맨 앞)다(D-354). 포커스를 받으면 글자 끝이고 ←·→·Home·End 가 옮긴다. 시스템이 글자 수 안으로 가둔다.
        JBRO_FIELD(Int32, caretIndex, NoSerialize() | ReadOnly() | Category("State")) = 0;
        // 선택의 다른 끝(코드포인트 번호)이다(D-355). -1 이면 선택이 없다. 커서와 이것 사이가 선택 구간이다. 스크립트가 써도 된다(핸들의 `SetSelection`).
        JBRO_FIELD(Int32, selectionAnchor, NoSerialize() | ReadOnly() | Category("State")) = -1;
        // IME 가 조합 중인가(D-355). 조합 글자는 `display` 에만 보이고 `text` 에는 없다.
        JBRO_FIELD(Bool, composing, NoSerialize() | ReadOnly() | Category("State")) = false;
        // 마지막으로 `display` 에 옮긴 글자의 판번호와 조합 글자를 끼워 옮겼는가, 깜빡임 시계다.
        JBRO_FIELD(UInt32, mirroredRevision, NoSerialize() | ReadOnly() | Hidden()) = 0;
        JBRO_FIELD(Bool, mirroredComposition, NoSerialize() | ReadOnly() | Hidden()) = false;
        JBRO_FIELD(Float, blinkTime, NoSerialize() | ReadOnly() | Hidden()) = 0.0f;
        // `SpriteSwap` 이 바꾸기 전에 떠 둔 보통 스프라이트와 바꿨는가(D-355, 버튼과 같다).
        JBRO_FIELD(AssetHandle, normalSprite, NoSerialize() | ReadOnly() | Hidden());
        JBRO_FIELD(Bool, swapped, NoSerialize() | ReadOnly() | Hidden()) = false;
    };
}
