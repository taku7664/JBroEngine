#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <JBro/Reflection/TypeDescriptor.h>

namespace JBro::Widget
{
    // enum 하나를 고르는 칸이다.
    //
    // 기존 엔진은 `magic_enum` 으로 이름을 얻었다. 우리는 **리플렉션이 이미
    // 이름표를 들고 있다**(`TypeDescriptor::enumNames`, D-56) - 같은 것을 두 번
    // 만들지 않는다. 그 대신 값의 주소와 설명자를 받는다.
    //
    // 돌려주는 값: 참이면 값이 바뀌었다.
    bool EnumCombo(const char* id, const EnumNames& names, void* value,
        float width = 0.0f);
}
