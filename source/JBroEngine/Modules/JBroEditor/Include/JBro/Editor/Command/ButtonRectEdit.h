#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Math2D.h>

#include <JBro/Core/StableTypeId.h>

namespace JBro
{
    class EditorApplication;
    class ComponentBase;
    struct ComponentAddress;

    // **사각형(`offset` 가운데의 `size`)을 가진 위젯의 두 필드다**(D-351). 버튼·토글·슬라이더가 같은 뜻의 두 필드를 둔다 -
    // 캔버스 뷰의 손잡이가 타입을 가리지 않고 같은 셈으로 고친다. 그런 타입이 아니면 거짓이다.
    Bool RectFieldsOf(ComponentBase& component, ComponentTypeId typeId, Vector2*& size, Vector2*& offset);

    // **위젯의 사각형을 커맨드 하나로 쓴다**(D-351 1 단계). `size` 와 `offset` 을 함께 바꾸므로 `CompoundCommand` 다 - 되돌리기는 하나다.
    // 지금 값과 같으면 커맨드를 만들지 않고 거짓이다. 캔버스 뷰의 손잡이와 인스펙터의 `스프라이트에 맞추기` 가 함께 쓴다.
    Bool SetButtonRect(EditorApplication& editor, const ComponentAddress& address, Vector2 size, Vector2 offset);
}
