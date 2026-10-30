#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Draggable2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Draggable2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
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
    // **끌 수 있는 것**이다(D-357, ui-plan §7.2). 사각형(`offset` 가운데의 `size`)을 누르고 끌면 이 오브젝트가 포인터를 따라온다(레이어 단위 - 부모가 돌거나 늘어나 있으면
    // 어긋난다). 놓으면 포인터 아래의 `DropTarget2D` 에 떨어진다 - 그쪽의 `dropped` 와 이쪽의 `droppedOn` 이 한 프레임 서로를 가리키고, 그쪽 스크립트가 `OnDrop`, 이쪽이
    // `OnDragBegin`·`OnDragEnd` 를 받는다. 놓을 자리가 없고 `snapBack` 이면 끌기 전 자리로 돌아간다. 스크롤 뷰 안이어도 끌기는 이것이 가진다.
    class Draggable2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Draggable2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Draggable2D)

        JBRO_FIELD(Vector2, size, Pixels()) { 64.0f, 64.0f };
        JBRO_FIELD(Vector2, offset, Pixels());
        JBRO_FIELD(Bool, interactable) = true;
        JBRO_FIELD(Bool, snapBack) = false;

        // 시스템이 채우는 이번 프레임의 상태다. `droppedOn` 은 놓은 그 프레임에만 차 있다.
        JBRO_FIELD(Bool, dragging, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Handle::GameObject, droppedOn, NoSerialize() | ReadOnly() | Category("State"));
        JBRO_FIELD(Vector2, startPosition, NoSerialize() | ReadOnly() | Hidden());
        JBRO_FIELD(Bool, startValid, NoSerialize() | ReadOnly() | Hidden()) = false;
    };
}
