#pragma once

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
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
        JBRO_FIELD(Vector2,  position);
        // **담는 것은 라디안이고 내주는 것은 도다**(D-248). 계산하는 쪽(행렬·물리·그리기)이
        // 매 프레임 읽으므로 저장은 라디안이라야 변환이 없고, 사람과 스크립트는 도로 만진다.
        // 값을 그대로 열어 두면 둘 중 하나가 반드시 틀리므로 닫고 접근자로만 연다.
        JBRO_FIELD_PRIVATE(Radian, rotation) = 0.0f;

    public:
        // 사용자 쪽. `SetRotation(90)` 이 직각이다.
        Degree GetRotation() const
        {
            return rotation.ToDegree();
        }

        void SetRotation(Degree value)
        {
            rotation = value;
        }

        // 계산 쪽. 변환이 없다.
        Radian GetRotationRadian() const
        {
            return rotation;
        }

        void SetRotationRadian(Radian value)
        {
            rotation = value;
        }

        JBRO_FIELD(Vector2,  scale) { 1.0f, 1.0f };
        // 화면 레이어의 루트가 붙는 화면의 점이다(D-237, 0..1, y 위: (0, 0) 이 왼쪽 아래, (1, 1) 이 오른쪽 위). `position` 은 그 점에서의 거리다.
        // 월드 레이어와 자식에는 뜻이 없다. 위치를 덮지 않고 루트의 부모 행렬로 들어간다 - 위치의 출처가 둘이 되지 않는다.
        JBRO_FIELD(Vector2,  anchor, Range(0, 1)) { 0.5f, 0.5f };

        // 시스템이 채우는 월드 캐시. worldValid 가 false 인 동안의 값은 읽지 않는다.
        //
        // **저장하지 않고 고칠 수도 없다.** 저작 값에서 다시 계산되는 것이라 파일에 적으면
        // 두 벌이 되고, 인스펙터에서 고쳐 봐야 다음 갱신에 덮인다. 인스펙터에 보이기는 하는
        // 편이 낫다 — 계층이 왜 그 자리에 있는지 볼 수 있는 유일한 창이다.
        JBRO_FIELD(Matrix3x2, world,         NoSerialize() | ReadOnly() | Category("World cache"));
        JBRO_FIELD(Vector2,      worldPosition, NoSerialize() | ReadOnly() | Category("World cache"));
        // 월드 캐시는 **라디안이다.** 시스템이 채우고 계산하는 쪽만 읽으므로 도일 이유가 없다.
        // 인스펙터에는 도로 보인다 - 그것은 코덱이 할 일이다(D-248).
        JBRO_FIELD(Radian,    worldRotation, NoSerialize() | ReadOnly() | Category("World cache")) = 0.0f;
        JBRO_FIELD(Vector2,      worldScale,    NoSerialize() | ReadOnly() | Category("World cache")) { 1.0f, 1.0f };
        JBRO_FIELD(bool,      worldValid,    NoSerialize() | ReadOnly() | Category("World cache")) = false;
    };
}
