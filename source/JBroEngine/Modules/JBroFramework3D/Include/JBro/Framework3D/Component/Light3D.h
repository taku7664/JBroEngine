#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Light3D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Light3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **3D 라이트**(D-407, tasks/lighting3d-plan.md). 붙인 오브젝트의 `Transform3D` 월드 자리에서, 오브젝트의 로컬 -Z 쪽으로 빛을 낸다(크기는 보지 않는다).
    // 2D 의 `Light2D` 와 같은 모양이다 - 2D 의 `Global` 이 여기의 `Ambient` 다. 모든 레이어의 메시를 비추고, 감춘 레이어의 라이트는 꺼진다.
    // **캔버스에 라이트가 하나도 없으면** 메시는 렌더러의 기본 빛(고정 방향광과 주변광)을 받는다. 하나라도 있으면 놓인 라이트만 빛이다.
    // `Unlit` 재질과 발광은 빛을 받지 않는다. 한 오브젝트에 여럿 붙일 수 있다(방향광과 환경광을 함께).
    class Light3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Light3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Light3D)

        JBRO_FIELD(Light3DType, type) = Light3DType::Point;
        // 선형 색이다.
        JBRO_FIELD(Color, color) { 1.0f, 1.0f, 1.0f, 1.0f };
        // 색에 곱한다. `Directional` 은 마주 보는 흰 면이, `Point`·`Spot` 은 1 유닛 거리의 그 면이 이 밝기가 된다(거리 제곱으로 준다).
        JBRO_FIELD(Float, intensity, Clamp(0.0f, 100.0f)) = 1.0f;
        // `Point`·`Spot` 의 빛이 0 이 되는 거리다(월드 단위, glTF 의 감쇠). 0 이면 끝이 없다.
        JBRO_FIELD(Float, range, Clamp(0.0f, 1000.0f)) = 10.0f;
        // `Spot` 의 원뿔 **전체** 각이다. 안쪽 각 안은 빛이 다 닿고 바깥 각에서 0 이 된다.
        JBRO_FIELD(Degree, innerAngle, Clamp(0.0f, 179.0f)) = 30.0f;
        JBRO_FIELD(Degree, outerAngle, Clamp(0.0f, 179.0f)) = 60.0f;
        // **그림자를 드리우는가**(D-445). 뷰마다 첫 `Directional` 하나와 `Spot` 넷까지다. `Point`·`Ambient` 는 아직 그림자가 없다.
        JBRO_FIELD(Bool, castShadows) = false;
        // 그림자의 짙기다. 1 이면 그림자 안에 이 빛이 닿지 않고, 0 이면 그림자가 없는 것과 같다.
        JBRO_FIELD(Float, shadowStrength, Clamp(0.0f, 1.0f)) = 1.0f;
    };
}
