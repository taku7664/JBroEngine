#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`ScrollView2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::ScrollView2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
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
    // **끌어서 내용을 움직이는 창**이다(D-351, ui-plan §6.2 3 단계). 뷰포트는 **같은 오브젝트의 `Mask2D`** 사각형이고(없으면 스크롤하지 않는다),
    // `content` 는 움직이는 자식 오브젝트다. 내용의 사각형은 `contentSize` 를 `content` 의 자리 가운데에 둔 것이고, 뷰포트가 늘 내용 안에 들도록
    // 자리를 가둔다(내용이 뷰포트보다 작은 축은 움직이지 않는다).
    //
    // 포인터는 `Button2DSystem` 이 본다 - 뷰포트 안에서 누르고 몇 픽셀 끌면 끌기가 시작되고, 그때 안의 버튼 누름은 취소된다(뗌만 있고 누름은 없다).
    // 놓으면 속도로 미끄러지다(`decelerationRate` 는 1 초에 남는 비율) 멈춘다. 휠은 포인터가 뷰포트 안일 때 `scrollSensitivity` 픽셀씩이다.
    class ScrollView2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ScrollView2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(ScrollView2D)

        JBRO_FIELD(Handle::GameObject, content, Tooltip("끌면 옮기는 내용 오브젝트. 이 오브젝트의 자식이어야 한다"));
        JBRO_FIELD(Vector2, contentSize, Pixels()) { 320.0f, 480.0f };
        // 스크롤바(D-355): `Slider2D` 가 붙은 오브젝트다. 값 0 이 위·왼쪽이고 시스템이 매 프레임 내용 자리와 맞춘다 - 막대를 끌면 내용이, 내용이 움직이면 막대가 따라온다.
        // 막대의 손잡이 스프라이트가 `Custom` 크기면 그 축 길이를 뷰포트/내용 비율로 둔다(최소 트랙의 1/10). 내용이 뷰포트보다 작으면 값 0 이고 손잡이는 트랙 전체다.
        // 손잡이 밖의 트랙을 누르면 포인터 쪽으로 뷰포트 하나만큼 가고, 쥐고 있으면 손잡이가 포인터에 닿을 때까지 되풀이한다(D-360). 손잡이 위를 누르면 끌기다.
        JBRO_FIELD(Handle::GameObject, horizontalBar, Tooltip("가로 스크롤바 - Slider2D 가 붙은 오브젝트. 값 0 이 왼쪽이다"));
        JBRO_FIELD(Handle::GameObject, verticalBar, Tooltip("세로 스크롤바 - Slider2D 가 붙은 오브젝트. 값 0 이 위다"));
        JBRO_FIELD(Bool, horizontal) = false;
        JBRO_FIELD(Bool, vertical) = true;
        // 휠 한 칸에 움직이는 양(레이어 단위 - 화면 레이어는 기준 픽셀).
        JBRO_FIELD(Float, scrollSensitivity, Pixels()) = 40.0f;
        // 놓은 뒤 1 초에 남는 속도의 비율이다. 0 이면 바로 멈춘다.
        JBRO_FIELD(Float, decelerationRate, Range(0.0f, 1.0f)) = 0.135f;

        // 시스템이 채우는 이번 프레임의 상태다.
        JBRO_FIELD(Bool, dragging, NoSerialize() | ReadOnly() | Category("State")) = false;
        JBRO_FIELD(Vector2, velocity, NoSerialize() | ReadOnly() | Category("State"));
    };
}
