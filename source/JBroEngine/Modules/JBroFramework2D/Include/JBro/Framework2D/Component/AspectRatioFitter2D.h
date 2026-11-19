#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`AspectRatioFitter2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::AspectRatioFitter2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **가로세로비 맞추기**다(D-358, ui-plan §7.2). 같은 오브젝트의 스프라이트(`Custom` 크기)·마스크·위젯 사각형의 한 변을 다른 변과 `aspect`(폭/높이)로 매 프레임 맞춘다.
    // `ContentSizeFitter2D` 뒤에 돈다 - 내용 크기로 폭을 잡고 높이를 비율로 하는 식이다. `Layout2DSystem` 이 돈다.
    class AspectRatioFitter2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::AspectRatioFitter2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(AspectRatioFitter2D)

        JBRO_FIELD(AspectMode, mode) = AspectMode::WidthControlsHeight;
        JBRO_FIELD(Float, aspect, Range(0.01f, 100.0f)) = 1.0f;
    };
}
