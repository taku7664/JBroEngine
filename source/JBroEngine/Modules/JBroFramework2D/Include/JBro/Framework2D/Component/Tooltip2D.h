#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Tooltip2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Tooltip2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
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
    // **툴팁**이다(D-357, ui-plan §7.2). 포인터가 사각형(`offset` 가운데의 `size`) 위에 누르지 않고 `delay` 초 머물면 `target` 오브젝트를 켜고, 떠나거나 누르면 끈다.
    // 포인터를 잡지 않는다 - 같은 자리의 버튼은 그대로 눌린다. 모달이 켜져 있으면 그 안의 것만 머문 것으로 센다. `target` 이 이 오브젝트 자신이면 손대지 않는다.
    // `follow` 면 보이는 동안 `target` 을 포인터 자리 + `followOffset`(`target` 부모의 좌표)에 둔다(D-360) - 아니면 `target` 은 제자리다.
    class Tooltip2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Tooltip2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Tooltip2D)

        JBRO_FIELD(Vector2, size, Pixels()) { 160.0f, 32.0f };
        JBRO_FIELD(Vector2, offset, Pixels());
        JBRO_FIELD(Float, delay, Range(0.0f, 5.0f)) = 0.5f;
        JBRO_FIELD(Handle::GameObject, target, Tooltip("머문 뒤 켤 오브젝트. 보통 이 오브젝트의 자식이다"));
        JBRO_FIELD(Bool, follow, Tooltip("보이는 동안 target 을 포인터 자리에 둔다")) = false;
        JBRO_FIELD(Vector2, followOffset, Pixels()) { 12.0f, -12.0f };

        // 시스템이 채우는 이번 프레임의 상태다.
        JBRO_FIELD(Bool, shown, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Float, hoverTime, NoSerialize() | ReadOnly() | Hidden()) = 0.0f;
    };
}
