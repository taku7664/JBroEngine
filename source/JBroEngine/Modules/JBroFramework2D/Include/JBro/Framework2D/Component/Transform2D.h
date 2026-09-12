#pragma once

#include <JBro/Framework2D/Math2D.h>
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

        // 저작 값
        Vec2  position;
        float rotation = 0.0f;
        Vec2  scale{ 1.0f, 1.0f };

        // 시스템이 채우는 월드 캐시. worldValid 가 false 인 동안의 값은 읽지 않는다.
        Matrix3x2 world;
        Vec2      worldPosition;
        float     worldRotation = 0.0f;
        Vec2      worldScale{ 1.0f, 1.0f };
        bool      worldValid = false;
    };
}
