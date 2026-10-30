#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`VirtualKeyboard2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::VirtualKeyboard2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

namespace JBro::Component
{
    // **가상 키보드**다(D-358, ui-plan §7.2). 이 오브젝트 아래의 버튼을 누르면 글자 칸에 글자가 들어간다 - `target` 의 `InputField2D`, 비면 지금 포커스를 가진 칸이다.
    // 버튼은 포커스를 가져가지 않는다(칸이 포커스를 지킨다). `backspace`·`enter`·`space` 가 가리키는 버튼은 그 구실이고, 나머지 버튼은 제 오브젝트의 `Text2D` 글자를 넣는다
    // (글자가 없으면 아무것도 넣지 않는다). 글자 칸의 상한·선택은 키보드로 친 것과 같다.
    class VirtualKeyboard2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::VirtualKeyboard2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(VirtualKeyboard2D)

        JBRO_FIELD(Handle::GameObject, target, Tooltip("글자를 넣을 칸의 오브젝트. 비면 포커스를 가진 칸이다"));
        JBRO_FIELD(Handle::GameObject, backspace, Tooltip("앞 글자를 지우는 버튼"));
        JBRO_FIELD(Handle::GameObject, enter, Tooltip("제출하는 버튼"));
        JBRO_FIELD(Handle::GameObject, space, Tooltip("띄어쓰기를 넣는 버튼"));
    };
}
