#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Mask2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Mask2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **자손을 사각형 안에서만 그린다**(D-351, ui-plan §6.2 3 단계). 사각형은 `offset` 가운데의 `size`(로컬 좌표, 버튼과 같은 뜻)이고,
    // 이 오브젝트의 **자손**(자신 포함)의 스프라이트·글자가 그 밖으로 나가면 잘린다. 돌린 마스크는 둘러싼 축 정렬 사각형으로 근사한다
    // (스텐실이 아니라 시저다). 마스크가 겹치면 교집합이다. 포인터도 사각형 밖이면 자손의 위젯을 누르지 못한다.
    //
    // `Mask2DSystem` 이 트랜스폼 전파 뒤에 레이어 좌표의 사각형(`clip*`)을 채우고, 렌더 추출과 버튼 판정이 그것을 읽는다.
    class Mask2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Mask2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Mask2D)

        JBRO_FIELD(Vector2, size) { 320.0f, 240.0f };
        JBRO_FIELD(Vector2, offset);

        // 시스템이 채우는 이번 프레임의 사각형이다(레이어 좌표 - 화면 레이어는 기준 픽셀, 월드는 유닛). `clipValid` 가 거짓이면 자르지 않는다.
        JBRO_FIELD(Bool, clipValid, NoSerialize() | ReadOnly() | Hidden()) = false;
        JBRO_FIELD(Float, clipMinX, NoSerialize() | ReadOnly() | Hidden()) = 0.0f;
        JBRO_FIELD(Float, clipMinY, NoSerialize() | ReadOnly() | Hidden()) = 0.0f;
        JBRO_FIELD(Float, clipMaxX, NoSerialize() | ReadOnly() | Hidden()) = 0.0f;
        JBRO_FIELD(Float, clipMaxY, NoSerialize() | ReadOnly() | Hidden()) = 0.0f;
    };
}
