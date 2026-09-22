#pragma once

#include <JBro/Runtime/Component.h>
#include <JBro/Types/NameTable.h>

namespace JBro
{
    class EditorApplication;
    class GameObject;

    namespace Widget
    {
        class FormLayout;
    }

    // **필드 줄 밑에 붙는 한 줄이다**(D-165). 기존 인스펙터는 `FrameIndex` 줄 밑에 "프레임 고르기" 단추를 두려고
    // 타입마다 그리는 법을 직접 알았다. 우리 인스펙터는 **컴포넌트 타입을 모른다** - 그 지식은 이 표에 모인다.
    // 인스펙터는 컴포넌트의 맨 위 필드를 그릴 때마다 (타입, 필드 이름)으로 이 표를 묻기만 한다.
    //
    // 매 프레임 도는 길이라 글자를 견주지 않는다. 타입은 `ComponentTypeId`, 필드는 인턴한 `NameId` 다.
    struct FieldExtraContext
    {
        EditorApplication* editor = nullptr;
        GameObject* owner = nullptr;
        ComponentBase* component = nullptr;
        ComponentTypeId typeId = 0;
    };

    using FieldExtraDraw = void (*)(Widget::FormLayout& layout, const FieldExtraContext& context);

    // 없으면 nullptr 이다.
    FieldExtraDraw FindFieldExtra(ComponentTypeId typeId, NameId field);
}
