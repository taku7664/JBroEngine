#pragma once

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    // **누를 수 있는 사각형**이다(D-237, ui-plan §2.5). 누름 사각형은 오브젝트의 로컬 좌표로 `offset` 을 가운데에 둔 `size` 이고,
    // 트랜스폼의 위치·회전·크기를 따른다. 화면 레이어에서는 기준 픽셀, 월드 레이어에서는 월드 유닛이다.
    //
    // `Button2DSystem` 이 입력 레이어 `"UI"` 에서 포인터(마우스 왼쪽 단추, 첫 손가락)를 보고 상태를 채운다. 버튼 위의 포인터는
    // 거기서 소비된다 - 아래 레이어의 핸들러와 폴링에는 빈 마우스·터치만 남는다. 같은 오브젝트의 스크립트는
    // `GameScript2D::OnPointerEnter`·`OnPointerExit`·`OnPointerDown`·`OnPointerUp`·`OnClick` 을 받는다.
    //
    // 같은 오브젝트에 `SpriteRenderer2D` 가 있고 `tintSprite` 가 켜져 있으면 상태에 따라 그 `tint` 를 바꾼다.
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

        JBRO_FIELD(Vector2, size) { 160.0f, 48.0f };
        JBRO_FIELD(Vector2, offset);
        // 꺼져 있으면 누르지 못하고 `disabledTint` 로 보인다. 포인터는 그래도 소비한다 - 꺼진 단추 뒤의 게임이 눌리면 안 된다.
        JBRO_FIELD(bool, interactable) = true;
        JBRO_FIELD(bool, tintSprite) = true;
        JBRO_FIELD(Color, normalTint) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Color, hoverTint) { 0.9f, 0.9f, 0.9f, 1.0f };
        JBRO_FIELD(Color, pressedTint) { 0.7f, 0.7f, 0.7f, 1.0f };
        JBRO_FIELD(Color, disabledTint) { 0.5f, 0.5f, 0.5f, 0.6f };

        // 시스템이 채우는 이번 프레임의 상태다. 스크립트가 훅 대신 읽어도 된다.
        // `clicked` 는 버튼 위에서 눌렀다가 버튼 위에서 뗀 그 프레임에만 참이다.
        JBRO_FIELD(bool, hovered, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(bool, pressed, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(bool, clicked, NoSerialize() | ReadOnly() | Category("State")) = false;
    };
}
