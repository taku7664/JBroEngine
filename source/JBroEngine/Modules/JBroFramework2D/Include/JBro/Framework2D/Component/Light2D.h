#pragma once

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Angle.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // 라이트의 종류다(D-291). 값의 차례는 파일과 렌더러의 `Light2DKind` 가 함께 쓰므로 뒤에만 더한다.
    enum class Light2DType : std::uint8_t
    {
        // 캔버스 전체에 같은 빛이다(환경광). 위치·반지름·각도를 보지 않는다. 여럿이면 더한다.
        Global,
        // 둘레로 퍼지는 빛이다.
        Point,
        // 오브젝트의 오른쪽(+x)을 향한 원뿔이다. 오브젝트를 돌리면 함께 돈다.
        Spot,
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::Light2DType, "Component::Light2DType",
        { Component::Light2DType::Global, "Global" },
        { Component::Light2DType::Point,  "Point" },
        { Component::Light2DType::Spot,   "Spot" });
}

namespace JBro::Component
{
    // **2D 라이트**(D-291, tasks/lighting2d-plan.md). 붙인 오브젝트의 `Transform2D` 월드 위치에서 빛을 낸다. 빛을 받는 레이어(`Layer::lit`)의
    // 스프라이트·글자만 밝아진다. 캔버스에 라이트가 하나도 없으면 라이팅이 꺼진 장면이라 모두 원래 색이다 - 하나라도 있으면 빛이 닿지 않는 곳은
    // `Global` 라이트(환경광)만큼만 밝다.
    //
    // 라이트가 놓인 레이어는 빛이 닿는 곳을 정하지 않는다 - 빛을 받는 모든 레이어를 비춘다. 레이어가 감춰져 있으면 그 레이어의 라이트도 꺼진다.
    class Light2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Light2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Light2D)

        JBRO_FIELD(Light2DType, type) = Light2DType::Point;
        JBRO_FIELD(Color, color) { 1.0f, 1.0f, 1.0f, 1.0f };
        // 색에 곱한다. 1 을 넘으면 흰색보다 밝은 빛이 되어 어두운 색을 더 밝힌다.
        JBRO_FIELD(Float, intensity, Range(0.0f, 100.0f)) = 1.0f;
        // 이 거리(월드 단위)까지는 빛이 다 닿고, `outerRadius` 에서 0 이 된다. `Global` 은 보지 않는다.
        JBRO_FIELD(Float, innerRadius, Range(0.0f, 1000.0f)) = 0.0f;
        JBRO_FIELD(Float, outerRadius, Range(0.0f, 1000.0f)) = 5.0f;
        // `Spot` 의 원뿔 **전체** 각이다. 안쪽 각 안은 빛이 다 닿고 바깥 각에서 0 이 된다.
        JBRO_FIELD(Degree, innerAngle, Range(0.0f, 360.0f)) = 30.0f;
        JBRO_FIELD(Degree, outerAngle, Range(0.0f, 360.0f)) = 60.0f;
        // 참이면 `ShadowCaster2D` 가 이 빛을 가린다. 그림자를 드리우는 라이트는 하나씩 그려져 많이 켜면 비싸다. `Global` 은 보지 않는다.
        JBRO_FIELD(Bool, castShadows) = false;
        // 그림자 가장자리가 번지는 정도다 - 빛을 이 반지름(월드 단위)의 원판으로 본다. 가림막에서 멀수록 넓게 번지고, 0 이면 가장자리가 단단하다.
        JBRO_FIELD(Float, shadowSoftness, Range(0.0f, 100.0f)) = 0.0f;
    };
}
