#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`CanvasGroup2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::CanvasGroup2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **그룹**이다(D-358, ui-plan §7.2). 이 오브젝트와 자손의 스프라이트·글자 알파에 `alpha` 를 곱하고(그룹이 겹치면 곱), `interactable` 이 거짓이면 아래의 위젯이 포인터·포커스를
    // 받지 않는다(마스크 밖처럼 없는 것이다). 그리기는 추출 때 곱한다 - 틴트 값은 바꾸지 않는다.
    // `blocksRaycasts` 가 거짓이면(D-361) 아래의 위젯은 포인터만 지나간다 - 호버·누름이 없고 그 누름은 게임이 본다. 키보드·게임패드 포커스는 그대로다.
    class CanvasGroup2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::CanvasGroup2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(CanvasGroup2D)

        JBRO_FIELD(Float, alpha, Range(0.0f, 1.0f)) = 1.0f;
        JBRO_FIELD(Bool, interactable) = true;
        JBRO_FIELD(Bool, blocksRaycasts, Tooltip("거짓이면 아래 위젯을 포인터가 지나간다. 포커스는 그대로다")) = true;
    };
}
