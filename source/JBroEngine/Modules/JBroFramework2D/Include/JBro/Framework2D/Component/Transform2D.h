#pragma once

#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    // 로컬 값과 그것으로 계산된 월드 캐시를 한 컴포넌트에 둔다(D-47).
    //
    // 둘로 나누면 사용자가 항상 쌍으로 붙여야 하고, 하나를 빠뜨리면 렌더·카메라·물리에서
    // 조용히 제외된다. 붙이는 방법이 사용자 프로젝트에 퍼지면 되돌릴 수 없으므로 하나로 둔다.
    //
    // world* 필드는 Transform2DSystem 이 쓴다. 스크립트는 읽기만 한다 —
    // 직접 쓰면 다음 갱신에서 덮이고, 그 사이 프레임만 어긋난다.
    class Transform2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Transform2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Transform2D)

        // 저작 값
        JBRO_FIELD(Vec2,  position);
        JBRO_FIELD(float, rotation) = 0.0f;
        JBRO_FIELD(Vec2,  scale) { 1.0f, 1.0f };

        // 시스템이 채우는 월드 캐시. worldValid 가 false 인 동안의 값은 읽지 않는다.
        //
        // **저장하지 않고 고칠 수도 없다.** 저작 값에서 다시 계산되는 것이라 파일에 적으면
        // 두 벌이 되고, 인스펙터에서 고쳐 봐야 다음 갱신에 덮인다. 인스펙터에 보이기는 하는
        // 편이 낫다 — 계층이 왜 그 자리에 있는지 볼 수 있는 유일한 창이다.
        JBRO_FIELD(Matrix3x2, world,         NoSerialize() | ReadOnly() | Category("World cache"));
        JBRO_FIELD(Vec2,      worldPosition, NoSerialize() | ReadOnly() | Category("World cache"));
        JBRO_FIELD(float,     worldRotation, NoSerialize() | ReadOnly() | Category("World cache")) = 0.0f;
        JBRO_FIELD(Vec2,      worldScale,    NoSerialize() | ReadOnly() | Category("World cache")) { 1.0f, 1.0f };
        JBRO_FIELD(bool,      worldValid,    NoSerialize() | ReadOnly() | Category("World cache")) = false;
    };
}
