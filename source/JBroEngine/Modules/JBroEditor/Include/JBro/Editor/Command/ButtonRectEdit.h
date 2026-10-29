#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Math2D.h>

namespace JBro
{
    class EditorApplication;
    struct ComponentAddress;

    // **버튼의 누름 사각형을 커맨드 하나로 쓴다**(D-351 1 단계). `size` 와 `offset` 을 함께 바꾸므로 `CompoundCommand` 다 - 되돌리기는 하나다.
    // 지금 값과 같으면 커맨드를 만들지 않고 거짓이다. 캔버스 뷰의 손잡이와 인스펙터의 `스프라이트에 맞추기` 가 함께 쓴다.
    Bool SetButtonRect(EditorApplication& editor, const ComponentAddress& address, Vector2 size, Vector2 offset);
}
