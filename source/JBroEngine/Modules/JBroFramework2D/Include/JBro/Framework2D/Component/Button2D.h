#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Button2D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Button2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **누를 수 있는 사각형**이다(D-237, ui-plan §2.5). 누름 사각형은 오브젝트의 로컬 좌표로 `offset` 을 가운데에 둔 `size` 이고,
    // 트랜스폼의 위치·회전·크기를 따른다. 화면 레이어에서는 기준 픽셀, 월드 레이어에서는 월드 유닛이다. **그림을 보지 않는다** -
    // 상태마다 그림이 달라도 누르는 자리는 그대로다(그림을 따라가면 호버 그림이 작을 때 들어왔다 나갔다를 되풀이한다).
    //
    // `Button2DSystem` 이 입력 레이어 `"UI"` 에서 포인터(마우스 왼쪽 단추, 첫 손가락)를 보고 상태를 채운다. 버튼 위의 포인터는
    // 거기서 소비된다 - 아래 레이어의 핸들러와 폴링에는 빈 마우스·터치만 남는다. 같은 오브젝트의 스크립트는
    // `GameScript2D::OnPointerEnter`·`OnPointerExit`·`OnPointerDown`·`OnPointerUp`·`OnClick` 을 받는다.
    //
    // **전이**(D-351)는 `targetGraphic`(비면 같은 오브젝트의 `SpriteRenderer2D`)에 건다. `ColorTint` 는 상태 색으로 `tint` 를 덮고,
    // `SpriteSwap` 은 상태 스프라이트로 `sprite` 를 바꾸며 보통 상태로 돌아올 때 떠 둔 원래 스프라이트를 되돌린다. 비운 상태 칸은 보통 그림 그대로다.
    class Button2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Button2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Button2D)

        JBRO_FIELD(Vector2, size, Pixels()) { 160.0f, 48.0f };
        JBRO_FIELD(Vector2, offset, Pixels());
        // 꺼져 있으면 누르지 못하고 꺼진 상태로 보인다. 포인터는 그래도 소비한다 - 꺼진 단추 뒤의 게임이 눌리면 안 된다.
        JBRO_FIELD(Bool, interactable) = true;
        JBRO_FIELD(ButtonTransition, transition, Category("Transition")) = ButtonTransition::ColorTint;
        // 전이를 거는 그림이다. 비면 같은 오브젝트의 스프라이트다.
        JBRO_FIELD(Handle::SpriteRenderer2D, targetGraphic, Tooltip("전이를 거는 스프라이트. 비면 같은 오브젝트의 SpriteRenderer2D 다") | Category("Transition"));
        JBRO_FIELD(Color, normalTint, Category("Transition")) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Color, hoverTint, Category("Transition")) { 0.9f, 0.9f, 0.9f, 1.0f };
        JBRO_FIELD(Color, pressedTint, Category("Transition")) { 0.7f, 0.7f, 0.7f, 1.0f };
        JBRO_FIELD(Color, disabledTint, Category("Transition")) { 0.5f, 0.5f, 0.5f, 0.6f };
        // `SpriteSwap` 의 상태 스프라이트다. 아이디가 저장되는 쪽이고 핸들은 해석 패스가 채운다(`SpriteRenderer2D::spriteId` 와 같다).
        JBRO_FIELD(AssetId, hoverSpriteId, Category("Transition"));
        JBRO_FIELD(AssetHandle, hoverSprite, NoSerialize() | ReadOnly() | Tooltip("hoverSpriteId 에서 해석된 값") | Hidden());
        JBRO_FIELD(AssetId, pressedSpriteId, Category("Transition"));
        JBRO_FIELD(AssetHandle, pressedSprite, NoSerialize() | ReadOnly() | Tooltip("pressedSpriteId 에서 해석된 값") | Hidden());
        JBRO_FIELD(AssetId, disabledSpriteId, Category("Transition"));
        JBRO_FIELD(AssetHandle, disabledSprite, NoSerialize() | ReadOnly() | Tooltip("disabledSpriteId 에서 해석된 값") | Hidden());

        // 시스템이 채우는 이번 프레임의 상태다. 스크립트가 훅 대신 읽어도 된다.
        // `clicked` 는 버튼 위에서 눌렀다가 버튼 위에서 뗀 그 프레임에만 참이다.
        JBRO_FIELD(Bool, hovered, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, pressed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, clicked, NoSerialize() | ReadOnly() | Category("State")) = false;
        // 포커스(D-352) - 게임패드·키보드가 가리키는 위젯이다. 호버와 같이 보인다.
        JBRO_FIELD(Bool, focused, NoSerialize() | ReadOnly() | Category("State")) = false;

        // `SpriteSwap` 이 대상의 스프라이트를 바꾸기 전에 떠 둔 보통 스프라이트다(기존 엔진의 normal 래치). `swapped` 가 참인 동안만 뜻이 있다.
        JBRO_FIELD(AssetHandle, normalSprite, NoSerialize() | ReadOnly() | Hidden());
        JBRO_FIELD(Bool, swapped, NoSerialize() | ReadOnly() | Hidden()) = false;
    };
}
