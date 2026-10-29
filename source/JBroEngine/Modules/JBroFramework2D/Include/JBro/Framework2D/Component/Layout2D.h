#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Layout2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Layout2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::Component
{
    // **자식을 칸에 늘어놓는다**(D-352, ui-plan §6.2 6 단계). 살아 있는 자식(계층 차례대로, 트랜스폼이 있는 것)을 `cellSize` 칸에 `spacing` 간격으로
    // 가로·세로·격자(`columns` 열)로 놓는다. 자식의 자리는 칸의 가운데이고, 블록 전체는 `pivot`(0..1, y 위)이 이 오브젝트의 원점에 오도록 놓는다 -
    // 기본 (0, 1) 은 왼쪽 위가 원점이다. `padding` 은 블록 안쪽 여백이다. 자식의 크기는 보지 않는다(앵커로 대부분 되므로 칸은 고정이다).
    //
    // `Layout2DSystem` 이 트랜스폼 전파 앞에 자식의 `position` 을 매 프레임 쓴다 - 자식의 자리는 이 컴포넌트가 소유한다. 블록의 크기는 `contentWidth`·
    // `contentHeight` 로 읽을 수 있다(스크롤 뷰의 `contentSize` 에 넣기 좋다).
    class Layout2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Layout2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Layout2D)

        JBRO_FIELD(LayoutMode, mode) = LayoutMode::Vertical;
        JBRO_FIELD(Vector2, cellSize) { 100.0f, 40.0f };
        JBRO_FIELD(Vector2, spacing) { 4.0f, 4.0f };
        JBRO_FIELD(Vector2, padding);
        JBRO_FIELD(Int32, columns, Range(1.0f, 64.0f)) = 2;
        JBRO_FIELD(Vector2, pivot, Range(0.0f, 1.0f)) { 0.0f, 1.0f };

        // 시스템이 채우는 블록의 크기다(여백 포함).
        JBRO_FIELD(Float, contentWidth, NoSerialize() | ReadOnly() | Category("State")) = 0.0f;
        JBRO_FIELD(Float, contentHeight, NoSerialize() | ReadOnly() | Category("State")) = 0.0f;
    };
}
