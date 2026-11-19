#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`DropTarget2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::DropTarget2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **놓을 자리**다(D-357, ui-plan §7.2). 끌리는 것이 사각형 위에 있으면 `hovering`, 거기서 놓이면 그 프레임에 `dropped` 가 그 오브젝트이고 같은 오브젝트의 스크립트가
    // `OnDrop` 을 받는다. 포인터를 잡지 않는다.
    class DropTarget2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::DropTarget2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(DropTarget2D)

        JBRO_FIELD(Vector2, size, Pixels()) { 128.0f, 128.0f };
        JBRO_FIELD(Vector2, offset, Pixels());

        // 시스템이 채우는 이번 프레임의 상태다. `dropped` 는 놓인 그 프레임에만 차 있다.
        JBRO_FIELD(Bool, hovering, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Handle::GameObject, dropped, NoSerialize() | ReadOnly() | Category("State"));
    };
}
