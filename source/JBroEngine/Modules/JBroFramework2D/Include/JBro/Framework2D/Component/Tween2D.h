#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Tween2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Tween2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Easing.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **트윈**이다(D-357, ui-plan §7.2). 같은 오브젝트의 자리·크기·회전(`Transform2D`)이나 틴트·알파(`SpriteRenderer2D`)를 `from` 에서 `to` 로 `duration` 초 동안 `ease` 모양으로
    // 옮긴다. `playOnStart` 면 처음 갱신에 저절로 돌고, 스크립트는 핸들의 `Play`·`Stop`·`Restart` 로 돌린다. `loop` 가 `Once` 면 끝에서 멈추고 그 프레임에 `finished` 다.
    // 값이 유한하지 않으면 그 프레임은 쓰지 않는다. `Tween2DSystem`(55) 이 위젯 뒤, 레이아웃 앞에 돈다.
    // `next` 가 가리키는 오브젝트의 `Tween2D` 는 이것이 끝에 닿은 프레임에 처음부터 돈다(D-360, 사슬) - 자기 자신은 무시한다(되풀이는 `loop`). 고리(A → B → A)는 된다.
    class Tween2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Tween2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Tween2D)

        JBRO_FIELD(TweenTarget, target) = TweenTarget::Position;
        JBRO_FIELD(EaseKind, ease) = EaseKind::Linear;
        JBRO_FIELD(Float, duration, Range(0.0f, 60.0f)) = 1.0f;
        JBRO_FIELD(TweenLoop, loop) = TweenLoop::Once;
        JBRO_FIELD(Bool, playOnStart) = true;
        JBRO_FIELD(Handle::GameObject, next, Tooltip("이것이 끝나면 돌릴 다음 트윈의 오브젝트. 자기 자신은 무시한다"));
        JBRO_FIELD(Vector2, fromVector);
        JBRO_FIELD(Vector2, toVector) { 1.0f, 0.0f };
        JBRO_FIELD(Color, fromColor) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Color, toColor) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Float, fromValue) = 0.0f;
        JBRO_FIELD(Float, toValue) = 1.0f;

        // 시스템이 채우는 이번 프레임의 상태다. `playing` 은 스크립트가 써도 된다. `finished` 는 `Once` 가 끝에 닿은 그 프레임에만 참이다.
        JBRO_FIELD(Bool, playing, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Float, time, NoSerialize() | ReadOnly() | Category("State")) = 0.0f;
        JBRO_FIELD(Bool, finished, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Bool, started, NoSerialize() | ReadOnly() | Hidden()) = false;
    };
}
