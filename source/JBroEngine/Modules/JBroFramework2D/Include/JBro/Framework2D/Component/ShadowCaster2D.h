#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`ShadowCaster2D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::ShadowCaster2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **2D 그림자를 드리운다**(D-291, tasks/lighting2d-plan.md 3 단계). 그림자를 드리우는 라이트(`Light2D::castShadows`)의 빛을 이 모양이 가린다.
    // 모양의 변을 라이트에서 밀어내 그리므로 화면 밖에 있어도, 라이트가 멀리 있어도 그림자가 맞다. 모양의 안쪽은 기본으로 밝다 - 벽 스프라이트가
    // 제 그림자에 묻히지 않는다. 감춘 레이어와 화면 레이어의 것은 그림자를 드리우지 않는다.
    class ShadowCaster2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ShadowCaster2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(ShadowCaster2D)

        JBRO_FIELD(ShadowShape2D, shape) = ShadowShape2D::Collider;
        // 참이면 모양의 안쪽도 제 그림자에 든다(라이트를 향한 변도 밀어낸다). 체인 콜라이더는 두께가 없어 늘 이렇다.
        JBRO_FIELD(Bool, selfShadow) = false;
    };
}
