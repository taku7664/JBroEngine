#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`ContentSizeFitter2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::ContentSizeFitter2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **내용 크기 맞추기**다(D-355, ui-plan §7.2). 같은 오브젝트의 내용 크기 - `Layout2D` 의 블록, 없으면 `Text2D` 의 글자 사각형(지난 프레임의 레이아웃) - 에
    // `padding` 을 두른 크기로 같은 오브젝트의 스프라이트(`sizeMode` 를 `Custom` 으로)·마스크·위젯 사각형의 `size` 를 매 프레임 쓴다. `fitWidth`·`fitHeight` 가 거짓인 축은
    // 손대지 않는다. `minSize` 보다 작아지지 않는다. 내용이 없으면 아무것도 하지 않는다. `Layout2DSystem` 이 레이아웃 뒤에 돈다.
    class ContentSizeFitter2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ContentSizeFitter2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(ContentSizeFitter2D)

        JBRO_FIELD(Bool, fitWidth) = true;
        JBRO_FIELD(Bool, fitHeight) = true;
        JBRO_FIELD(Vector2, padding, Pixels());
        JBRO_FIELD(Vector2, minSize, Pixels());

        // 시스템이 채우는 이번 프레임의 크기다(여백을 두른 것).
        JBRO_FIELD(Float, contentWidth, NoSerialize() | ReadOnly() | Category("State")) = 0.0f;
        JBRO_FIELD(Float, contentHeight, NoSerialize() | ReadOnly() | Category("State")) = 0.0f;
    };
}
